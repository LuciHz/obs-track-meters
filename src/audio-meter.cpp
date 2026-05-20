#include "audio-meter.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <cmath>

// ── Construction / Destruction ─────────────────────────────────────────────

AudioMeterWidget::AudioMeterWidget(QWidget *parent)
    : QWidget(parent)
{
    // Load persisted target values from OBS user config.
    // config_get_double returns the default if the key doesn't exist yet.
    config_t *cfg = obs_frontend_get_user_config();
    if (cfg) {
        config_set_default_double(cfg, CONFIG_SECTION,
                                  CONFIG_KEY_TARGET_MIN, TARGET_MIN_DB_DEFAULT);
        config_set_default_double(cfg, CONFIG_SECTION,
                                  CONFIG_KEY_TARGET_MAX, TARGET_MAX_DB_DEFAULT);
        m_targetMinDb = (float)config_get_double(cfg, CONFIG_SECTION,
                                                 CONFIG_KEY_TARGET_MIN);
        m_targetMaxDb = (float)config_get_double(cfg, CONFIG_SECTION,
                                                 CONFIG_KEY_TARGET_MAX);
    }

    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        m_peak[i].store(-96.0f);
        m_hold[i].store(-96.0f);
        m_clipped[i].store(false);
        m_trackEnabled[i].store(true);
        m_displayPeak[i]  = -96.0f;
        m_displayHold[i]  = -96.0f;
        m_holdTimer[i]    = 0;
        m_pendingClip[i]  = false;
    }

    setMinimumSize(200, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &AudioMeterWidget::onTimer);
    m_timer->start(33);
}

AudioMeterWidget::~AudioMeterWidget()
{
    m_timer->stop();
}

// ── Config persistence ─────────────────────────────────────────────────────

void AudioMeterWidget::saveConfig() const
{
    config_t *cfg = obs_frontend_get_user_config();
    if (!cfg)
        return;
    config_set_double(cfg, CONFIG_SECTION, CONFIG_KEY_TARGET_MIN, m_targetMinDb);
    config_set_double(cfg, CONFIG_SECTION, CONFIG_KEY_TARGET_MAX, m_targetMaxDb);
    config_save_safe(cfg, "tmp", nullptr);
}

// ── Target line setters ────────────────────────────────────────────────────

void AudioMeterWidget::setTargetMinDb(float db)
{
    m_targetMinDb = db;
    saveConfig();
    update();
}

void AudioMeterWidget::setTargetMaxDb(float db)
{
    m_targetMaxDb = db;
    saveConfig();
    update();
}

// ── setLevel — called from the audio thread ────────────────────────────────
//
// Everything here must be lock-free. Rules:
//   • Only store/exchange on atomics. Never a plain read-modify-write.
//   • m_hold uses compare_exchange_weak (atomic max) so it can't lose an
//     update if the GUI thread exchanges the value mid-operation.
//
void AudioMeterWidget::setLevel(int track, float dbfs)
{
    if (track < 0 || track >= MAX_AUDIO_MIXES)
        return;

    m_peak[track].store(dbfs, std::memory_order_release);

    if (dbfs >= DB_MAX)
        m_clipped[track].store(true, std::memory_order_release);

    // Atomic max: keep m_hold at the highest value seen since the last
    // GUI-thread exchange(DB_MIN). compare_exchange_weak loops until either:
    //   (a) we successfully raise the value, or
    //   (b) the current value is already >= dbfs (nothing to do).
    float current = m_hold[track].load(std::memory_order_relaxed);
    while (dbfs > current &&
           !m_hold[track].compare_exchange_weak(
               current, dbfs,
               std::memory_order_release,
               std::memory_order_relaxed))
    {}
}

// ── Track enable/disable ───────────────────────────────────────────────────
//
// setTrackEnabled: GUI thread.  isTrackEnabled: audio thread.
// m_trackEnabled is std::atomic<bool> — safe for both.
//
void AudioMeterWidget::setTrackEnabled(int track, bool enabled)
{
    if (track >= 0 && track < MAX_AUDIO_MIXES)
        m_trackEnabled[track].store(enabled, std::memory_order_release);
    update();
}

bool AudioMeterWidget::isTrackEnabled(int track) const
{
    if (track < 0 || track >= MAX_AUDIO_MIXES)
        return false;
    return m_trackEnabled[track].load(std::memory_order_acquire);
}

// ── Colour helper ──────────────────────────────────────────────────────────

float AudioMeterWidget::dbToPos(float db) const
{
    if (db <= DB_MIN) return 0.0f;
    if (db >= DB_MAX) return 1.0f;
    return (db - DB_MIN) / (DB_MAX - DB_MIN);
}

QColor AudioMeterWidget::colorAtDb(float db, float brightness) const
{
    float t            = dbToPos(db);
    float tBrightGreen = dbToPos(THRESH_BRIGHT_GREEN);
    float tYellow      = dbToPos(THRESH_YELLOW);
    float tOrange      = dbToPos(THRESH_ORANGE);
    float tRed         = dbToPos(THRESH_RED);

    int r, g, b;

    if (t <= tBrightGreen) {
        float local = (tBrightGreen > 0.0f) ? t / tBrightGreen : 0.0f;
        r = 0;
        g = (int)(80.0f + local * 175.0f);
        b = 0;
    } else if (t <= tYellow) {
        float local = (t - tBrightGreen) / (tYellow - tBrightGreen);
        r = (int)(local * 255.0f);
        g = 255;
        b = 0;
    } else if (t <= tOrange) {
        float local = (t - tYellow) / (tOrange - tYellow);
        r = 255;
        g = (int)(255.0f - local * 128.0f);
        b = 0;
    } else {
        float local = (t - tOrange) / (1.0f - tOrange);
        local = std::min(local, 1.0f);
        r = 255;
        g = (int)(127.0f - local * 127.0f);
        b = 0;
    }

    r = (int)(r * brightness);
    g = (int)(g * brightness);
    b = (int)(b * brightness);

    return QColor(std::min(255, std::max(0, r)),
                  std::min(255, std::max(0, g)),
                  std::min(255, std::max(0, b)));
}

// ── Clip warning ───────────────────────────────────────────────────────────

void AudioMeterWidget::showClipWarning()
{
    QStringList tracks;
    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        if (m_pendingClip[i]) {
            tracks << QString("Track %1").arg(i + 1);
            m_pendingClip[i] = false;
        }
    }
    if (tracks.isEmpty())
        return;

    QString msgText =
        QString("%1: Max Volume has been reached — "
                "your audio is in danger of clipping and distorting.")
        .arg(tracks.join(", "));

    QMessageBox *msg = new QMessageBox(this);
    msg->setWindowTitle("Clipping Warning");
    msg->setText(msgText);
    msg->setIcon(QMessageBox::Warning);
    msg->setAttribute(Qt::WA_DeleteOnClose);
    msg->setStandardButtons(QMessageBox::Ok);
    msg->show();
}

// ── onTimer — GUI thread only ──────────────────────────────────────────────

void AudioMeterWidget::onTimer()
{
    if (m_clipCooldown > 0)
        m_clipCooldown--;

    bool anyClipped = false;

    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        if (!m_trackEnabled[i].load(std::memory_order_acquire))
            continue;

        m_displayPeak[i] = m_peak[i].load(std::memory_order_acquire);

        bool clipped = m_clipped[i].exchange(false, std::memory_order_acq_rel);
        if (clipped) {
            m_pendingClip[i] = true;
            anyClipped = true;
        }

        float audioHold = m_hold[i].exchange(DB_MIN, std::memory_order_acq_rel);
        if (audioHold > m_displayHold[i]) {
            m_displayHold[i] = audioHold;
            m_holdTimer[i]   = PEAK_HOLD_TICKS;
        } else if (m_holdTimer[i] > 0) {
            m_holdTimer[i]--;
        } else {
            m_displayHold[i] -= 0.5f;
            if (m_displayHold[i] < DB_MIN)
                m_displayHold[i] = DB_MIN;
        }
    }

    if (anyClipped && m_clipCooldown == 0) {
        showClipWarning();
        m_clipCooldown = 300;
    }

    update();
}

// ── paintEvent ────────────────────────────────────────────────────────────

void AudioMeterWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();

    p.fillRect(0, 0, w, h, QColor(15, 15, 15));

    const int labelWidth  = 44;
    const int targetWidth = 36;
    const int leftPad     = labelWidth + targetWidth;
    const int meterHeight = h - 30;
    const int topPad      = 10;
    const int trackGap    = 5;

    int enabledCount = 0;
    for (int i = 0; i < MAX_AUDIO_MIXES; i++)
        if (m_trackEnabled[i].load(std::memory_order_relaxed)) enabledCount++;

    if (enabledCount == 0)
        return;

    const int totalMeterWidth = w - leftPad;
    const int barWidth = (totalMeterWidth - (enabledCount - 1) * trackGap)
                         / enabledCount;

    // Scale markings
    p.setPen(QColor(160, 160, 160));
    QFont scaleFont = p.font();
    scaleFont.setPointSize(7);
    p.setFont(scaleFont);

    const int scaleMarks[] = {0, -3, -6, -12, -18, -24, -36, -48, -60};
    for (int db : scaleMarks) {
        float pos = dbToPos((float)db);
        int   y   = topPad + (int)((1.0f - pos) * meterHeight);
        p.setPen(QColor(160, 160, 160));
        p.drawLine(targetWidth + labelWidth - 4, y,
                   targetWidth + labelWidth, y);
        p.drawText(targetWidth, y - 6, labelWidth - 6, 12,
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(db));
    }

    // Target labels — use live member variables, not compile-time constants
    {
        float posMin = dbToPos(m_targetMinDb);
        float posMax = dbToPos(m_targetMaxDb);
        int yMin = topPad + (int)((1.0f - posMin) * meterHeight);
        int yMax = topPad + (int)((1.0f - posMax) * meterHeight);

        QFont labelFont = p.font();
        labelFont.setPointSize(8);
        labelFont.setBold(true);
        p.setFont(labelFont);

        p.setPen(QColor(100, 220, 100));
        p.drawText(0, yMin - 9, targetWidth - 4, 18,
                   Qt::AlignRight | Qt::AlignVCenter, QString("Min"));
        p.setPen(QColor(220, 180, 50));
        p.drawText(0, yMax - 9, targetWidth - 4, 18,
                   Qt::AlignRight | Qt::AlignVCenter, QString("Max"));
    }

    // Draw meters
    int trackX = leftPad;
    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        if (!m_trackEnabled[i].load(std::memory_order_relaxed))
            continue;

        float peak  = m_displayPeak[i];
        float hold  = m_displayHold[i];
        int   peakH = (int)(dbToPos(peak) * meterHeight);
        int   holdY = topPad + (int)((1.0f - dbToPos(hold)) * meterHeight);

        for (int y = topPad; y < topPad + meterHeight; y++) {
            float db = DB_MIN + (1.0f - (float)(y - topPad) / meterHeight)
                       * (DB_MAX - DB_MIN);
            p.fillRect(trackX, y, barWidth, 1, colorAtDb(db, 0.18f));
        }

        if (peakH > 0) {
            int barTop = topPad + meterHeight - peakH;
            for (int y = barTop; y < topPad + meterHeight; y++) {
                float db = DB_MIN + (1.0f - (float)(y - topPad) / meterHeight)
                           * (DB_MAX - DB_MIN);
                p.fillRect(trackX, y, barWidth, 1, colorAtDb(db, 1.0f));
            }
        }

        if (hold > DB_MIN)
            p.fillRect(trackX, holdY, barWidth, 2, colorAtDb(hold, 1.0f));

        p.setPen(QColor(200, 200, 200));
        QFont trackFont = p.font();
        trackFont.setBold(true);
        trackFont.setPointSize(8);
        p.setFont(trackFont);
        p.drawText(trackX, topPad + meterHeight + 4, barWidth, 20,
                   Qt::AlignHCenter | Qt::AlignTop,
                   QString("T%1").arg(i + 1));

        trackX += barWidth + trackGap;
    }

    // Target lines drawn on top using live member variables
    auto drawTargetLine = [&](float db, QColor colour) {
        float pos = dbToPos(db);
        int   y   = topPad + (int)((1.0f - pos) * meterHeight);
        p.setPen(QPen(colour, 2, Qt::DashLine));
        p.drawLine(leftPad, y, w, y);
    };
    drawTargetLine(m_targetMinDb, QColor(100, 220, 100, 220));
    drawTargetLine(m_targetMaxDb, QColor(220, 180,  50, 220));
}

// ── SettingsDialog ─────────────────────────────────────────────────────────

SettingsDialog::SettingsDialog(AudioMeterWidget *meter, QWidget *parent)
    : QDialog(parent), m_meter(meter)
{
    setWindowTitle("Audio Meter Settings");
    setMinimumWidth(240);

    QVBoxLayout *layout = new QVBoxLayout(this);

    // ── Track visibility ───────────────────────────────────────────────────
    QGroupBox   *trackGroup  = new QGroupBox("Visible Tracks", this);
    QVBoxLayout *trackLayout = new QVBoxLayout(trackGroup);

    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        m_checkboxes[i] = new QCheckBox(QString("Track %1").arg(i + 1), this);
        m_checkboxes[i]->setChecked(meter->isTrackEnabled(i));

        int trackIndex = i;
        connect(m_checkboxes[i], &QCheckBox::toggled,
                this, [this, trackIndex](bool checked) {
                    if (m_meter)
                        m_meter->setTrackEnabled(trackIndex, checked);
                });

        trackLayout->addWidget(m_checkboxes[i]);
    }
    layout->addWidget(trackGroup);

    // ── Target lines ───────────────────────────────────────────────────────
    QGroupBox  *targetGroup  = new QGroupBox("Target Lines", this);
    QFormLayout *targetForm  = new QFormLayout(targetGroup);

    // Min target spinbox
    m_minSpinBox = new QDoubleSpinBox(this);
    m_minSpinBox->setRange(DB_MIN, DB_MAX);
    m_minSpinBox->setSingleStep(1.0);
    m_minSpinBox->setDecimals(1);
    m_minSpinBox->setSuffix(" dBFS");
    m_minSpinBox->setValue((double)meter->targetMinDb());
    m_minSpinBox->setToolTip("Lower target line (green). Aim to stay above this.");
    targetForm->addRow("Min target:", m_minSpinBox);

    // Max target spinbox
    m_maxSpinBox = new QDoubleSpinBox(this);
    m_maxSpinBox->setRange(DB_MIN, DB_MAX);
    m_maxSpinBox->setSingleStep(1.0);
    m_maxSpinBox->setDecimals(1);
    m_maxSpinBox->setSuffix(" dBFS");
    m_maxSpinBox->setValue((double)meter->targetMaxDb());
    m_maxSpinBox->setToolTip("Upper target line (yellow). Stay below this to avoid clipping.");
    targetForm->addRow("Max target:", m_maxSpinBox);

    layout->addWidget(targetGroup);

    // Apply values live as the user spins — validate so Min never exceeds Max
    connect(m_minSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double val) {
                if (!m_meter) return;
                // Clamp: min cannot be >= max
                if (val >= m_maxSpinBox->value())
                    val = m_maxSpinBox->value() - 0.5;
                // Block signals to avoid recursive validation
                m_minSpinBox->blockSignals(true);
                m_minSpinBox->setValue(val);
                m_minSpinBox->blockSignals(false);
                m_meter->setTargetMinDb((float)val);
            });

    connect(m_maxSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double val) {
                if (!m_meter) return;
                // Clamp: max cannot be <= min
                if (val <= m_minSpinBox->value())
                    val = m_minSpinBox->value() + 0.5;
                m_maxSpinBox->blockSignals(true);
                m_maxSpinBox->setValue(val);
                m_maxSpinBox->blockSignals(false);
                m_meter->setTargetMaxDb((float)val);
            });

    // ── Buttons ────────────────────────────────────────────────────────────
    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);

    setLayout(layout);
}
