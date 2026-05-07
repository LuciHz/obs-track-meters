#include <obs-module.h>
#include <obs-frontend-api.h>
#include <media-io/audio-io.h>

#include <QMainWindow>
#include <QDockWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <cmath>

#include "audio-meter.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-audio-meters", "en-US")

static AudioMeterWidget *g_meterWidget = nullptr;
static QDockWidget      *g_dock        = nullptr;

static void audio_callback(void *param, size_t mix_idx,
                            struct audio_data *data)
{
    AudioMeterWidget *widget = static_cast<AudioMeterWidget *>(param);
    if (!widget || !data)
        return;

    if (!widget->isTrackEnabled((int)mix_idx))
        return;

    const int frames = (int)data->frames;
    float     peak   = 0.0f;

    for (int ch = 0; ch < MAX_AV_PLANES; ch++) {
        if (!data->data[ch])
            break;
        const float *samples = reinterpret_cast<const float *>(data->data[ch]);
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
    if (!audio || !g_meterWidget)
        return;
    for (int i = 0; i < MAX_AUDIO_MIXES; i++)
        audio_output_disconnect(audio, i, audio_callback, g_meterWidget);
}

bool obs_module_load(void)
{
    obs_frontend_add_event_callback(
        [](enum obs_frontend_event event, void *) {
            if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
                QMainWindow *main =
                    static_cast<QMainWindow *>(obs_frontend_get_main_window());
                if (!main)
                    return;

                // Container widget with settings button and meter
                QWidget *container  = new QWidget();
                QVBoxLayout *vlay   = new QVBoxLayout(container);
                vlay->setContentsMargins(0, 0, 0, 0);
                vlay->setSpacing(2);

                // Settings button row — fixed height
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

                g_meterWidget = new AudioMeterWidget();
                g_meterWidget->setSizePolicy(QSizePolicy::Expanding,
                                             QSizePolicy::Expanding);

                vlay->addWidget(toolbar);
                vlay->addWidget(g_meterWidget, 1);
                container->setLayout(vlay);

                // Create dock and register with OBS frontend
                // so it appears in View > Docks menu
		g_dock = new QDockWidget("Audio Track Meters", main);
                g_dock->setObjectName("AudioTrackMetersDock");
                g_dock->setWidget(container);
                g_dock->setFeatures(QDockWidget::DockWidgetMovable |
                                    QDockWidget::DockWidgetFloatable |
                                    QDockWidget::DockWidgetClosable);
                g_dock->setMinimumHeight(200);
                g_dock->setMinimumWidth(200);

		// Physically dock it to the right side
                main->addDockWidget(Qt::RightDockWidgetArea, g_dock);

                // Register with OBS so it appears in View > Docks menu
                obs_frontend_add_dock_by_id("obs-audio-meters-dock",
                                            "Audio Track Meters",
                                            container);

                // Wire settings button
                QObject::connect(settBtn, &QToolButton::clicked, [=]() {
                    SettingsDialog *dlg =
                        new SettingsDialog(g_meterWidget, g_dock);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->exec();
                });

                // Connect audio callbacks
                audio_t *audio = obs_get_audio();
                for (int i = 0; i < MAX_AUDIO_MIXES; i++)
                    audio_output_connect(audio, i, nullptr,
                                         audio_callback, g_meterWidget);

                blog(LOG_INFO, "[obs-audio-meters] Loaded");

            } else if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
                disconnect_audio();
                g_meterWidget = nullptr;
                blog(LOG_INFO, "[obs-audio-meters] Audio disconnected");
            }
        },
        nullptr);

    return true;
}

void obs_module_unload(void)
{
    disconnect_audio();
    g_meterWidget = nullptr;
    g_dock        = nullptr;
    blog(LOG_INFO, "[obs-audio-meters] Unloaded");
}