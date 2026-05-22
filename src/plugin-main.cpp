#include <obs-module.h>
#include <obs-frontend-api.h>
#include <media-io/audio-io.h>

#include <QMainWindow>
#include <QDockWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <atomic>
#include <cmath>

#include "audio-meter.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-audio-meters", "en-US")

static std::atomic<AudioMeterWidget *> g_meterWidget{nullptr};

// Number of valid channels in the OBS audio mix, set once at startup.
// Only channels 0..(g_audioChannels-1) have valid data in audio_data::data[].
// Channels beyond this may be non-null but point to uninitialized/freed memory.
static int g_audioChannels = 2;

static void audio_callback(void *, size_t mix_idx,
                            struct audio_data *data)
{
    AudioMeterWidget *widget = g_meterWidget.load(std::memory_order_acquire);
    if (!widget)
        return;

    if (!data || data->frames == 0 || data->frames > 192000)
        return;

    if (mix_idx >= MAX_AUDIO_MIXES)
        return;

    const int frames   = (int)data->frames;
    const int channels = g_audioChannels;
    float     peak     = 0.0f;

    for (int ch = 0; ch < channels; ch++) {
        if (!data->data[ch])
            continue;

        const float *samples =
            reinterpret_cast<const float *>(data->data[ch]);
        for (int f = 0; f < frames; f++) {
            float s = fabsf(samples[f]);
            if (s > peak)
                peak = s;
        }
    }

    float dbfs = (peak > 0.00001f) ? 20.0f * log10f(peak) : -96.0f;
    if (dbfs < -96.0f) dbfs = -96.0f;
    if (dbfs >   0.0f) dbfs =   0.0f;

    widget->setLevel((int)mix_idx, dbfs);
}

static void disconnect_audio()
{
    audio_t *audio = obs_get_audio();
    if (!audio)
        return;

    AudioMeterWidget *widget =
        g_meterWidget.load(std::memory_order_acquire);
    if (!widget)
        return;

    for (int i = 0; i < MAX_AUDIO_MIXES; i++)
        audio_output_disconnect(audio, i, audio_callback, nullptr);

    g_meterWidget.store(nullptr, std::memory_order_release);
}

bool obs_module_load(void)
{
    obs_frontend_add_event_callback(
        [](enum obs_frontend_event event, void *) {
            static bool s_initialized = false;
            if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING && !s_initialized) {
                s_initialized = true;

                // Read the actual channel count from the audio output.
                audio_t *audio = obs_get_audio();
                if (audio) {
                    int ch = (int)audio_output_get_channels(audio);
                    if (ch >= 1 && ch <= MAX_AV_PLANES)
                        g_audioChannels = ch;
                }

                QWidget *container  = new QWidget();
                QVBoxLayout *vlay   = new QVBoxLayout(container);
                vlay->setContentsMargins(0, 0, 0, 0);
                vlay->setSpacing(2);

                QWidget *toolbar     = new QWidget();
                QHBoxLayout *hlay    = new QHBoxLayout(toolbar);
                hlay->setContentsMargins(4, 2, 4, 2);
                QToolButton *settBtn = new QToolButton();
                settBtn->setText("\u2699 Settings");
                settBtn->setToolTip("Configure visible tracks");
                hlay->addWidget(settBtn);
                hlay->addStretch();
                toolbar->setLayout(hlay);
                toolbar->setSizePolicy(QSizePolicy::Preferred,
                                       QSizePolicy::Fixed);

                AudioMeterWidget *widget = new AudioMeterWidget();
                widget->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Expanding);

                vlay->addWidget(toolbar);
                vlay->addWidget(widget, 1);
                container->setLayout(vlay);

                // obs_frontend_add_dock_by_id creates and manages the
                // QDockWidget wrapper itself — no double header.
                obs_frontend_add_dock_by_id("obs-audio-meters-dock",
                                            "Track Meters",
                                            container);

                // Get the dock OBS just created so Settings dialog can use it.
                QMainWindow *main =
                    static_cast<QMainWindow *>(obs_frontend_get_main_window());
                QDockWidget *dock = nullptr;
                if (main) {
                    for (QDockWidget *d : main->findChildren<QDockWidget *>()) {
                        if (d->objectName() == "obs-audio-meters-dock") {
                            dock = d;
                            break;
                        }
                    }
                }

                QObject::connect(settBtn, &QToolButton::clicked, [widget, dock]() {
                    SettingsDialog *dlg =
                        new SettingsDialog(widget, dock);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->exec();
                });

                g_meterWidget.store(widget, std::memory_order_release);

                for (int i = 0; i < MAX_AUDIO_MIXES; i++)
                    audio_output_connect(audio, i, nullptr,
                                         audio_callback, nullptr);

                blog(LOG_INFO,
                     "[obs-audio-meters] Loaded (channels: %d)",
                     g_audioChannels);

            } else if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
                disconnect_audio();
                blog(LOG_INFO, "[obs-audio-meters] Audio disconnected");
            }
        },
        nullptr);

    return true;
}

void obs_module_unload(void)
{
    disconnect_audio();
    blog(LOG_INFO, "[obs-audio-meters] Unloaded");
}
