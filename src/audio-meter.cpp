#include "audio-meter.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <cmath>

AudioMeterWidget::AudioMeterWidget(QWidget *parent)
    : QWidget(parent)
{
    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        m_peak[i].store(-96.0f);
        m_hold[i].store(-96.0f);
        m_clipped[i].store(false);
        m_displayPeak[i]  = -96.0f;
        m_displayHold[i]  = -96.0f;
        m_holdTimer[i]    = 0;
        m_trackEnabled[i] = true;
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

void AudioMeterWidget::setLevel(int track, float dbfs)
{
    if (track < 0 || track >= MAX_AUDIO_MIXES)
        return;

    m_peak[track].store(dbfs);

    if (dbfs >= DB_MAX)
        m_clipped[track].store(true);

    float current = m_hold[track].load();
    if (dbfs >= current)
        m_hold[track].store(dbfs);
}

void AudioMeterWidget::setTrackEnabled(int track, bool enabled)
{
    if (track >= 0 && track < MAX_AUDIO_MIXES)
        m_trackEnabled[track] = enabled;
    update();
}

bool AudioMeterWidget::isTrackEnabled(int track) const
{
    if (track < 0 || track >= MAX_AUDIO_MIXES)
        return false;
    return m_trackEnabled[track];
}

// Maps dBFS to 0.0-1.0 position
float AudioMeterWidget::dbToPos(float db) const
{
    if (db <= DB_MIN) return 0.0f;
    if (db >= DB_MAX) return 1.0f;
    return (db - DB_MIN) / (DB_MAX - DB_MIN);
}

// Returns the gradient colour at a given dBFS position
// brightness: 1.0 = full colour, 0.2 = dark background version
QColor AudioMeterWidget::colorAtDb(float db, float brightness) const
{
    // Normalise db to 0-1 within our range
    float t = dbToPos(db);

    // Define gradient stops matching our thresholds
    // dark green (0.0) -> bright green (THRESH_BRIGHT_GREEN) ->
    // yellow (THRESH_YELLOW) -> orange (THRESH_ORANGE) -> red (THRESH_RED+)

    float tBrightGreen = dbToPos(THRESH_BRIGHT_GREEN);
    float tYellow      = dbToPos(THRESH_YELLOW);
    float tOrange      = dbToPos(THRESH_ORANGE);
    float tRed         = dbToPos(THRESH_RED);

    int r, g, b;

    if (t <= tBrightGreen) {
        // Dark green to bright green
        float local = (tBrightGreen > 0.0f) ? t / tBrightGreen : 0.0f;
        r = 0;
        g = (int)(80.0f + local * 175.0f);   // 80 -> 255
        b = 0;
    } else if (t <= tYellow) {
        // Bright green to yellow
        float local = (t - tBrightGreen) / (tYellow - tBrightGreen);
        r = (int)(local * 255.0f);
        g = 255;
        b = 0;
    } else if (t <= tOrange) {
        // Yellow to orange
        float local = (t - tYellow) / (tOrange - tYellow);
        r = 255;
        g = (int)(255.0f - local * 128.0f);  // 255 -> 127
        b = 0;
    } else {
        // Orange to red
        float local = (t - tOrange) / (1.0f - tOrange);
        local = std::min(local, 1.0f);
        r = 255;
        g = (int)(127.0f - local * 127.0f);  // 127 -> 0
        b = 0;
    }

    r = (int)(r * brightness);
    g = (int)(g * brightness);
    b = (int)(b * brightness);

    return QColor(
        std::min(255, std::max(0, r)),
        std::min(255, std::max(0, g)),
        std::min(255, std::max(0, b))
    );
}

void AudioMeterWidget::showClipWarning()
{
    // Build list of clipped tracks
    QStringList tracks;
    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        if (m_pendingClip[i]) {
            tracks << QString("Track %1").arg(i + 1);
            m_pendingClip[i] = false;
        }
    }

    if (tracks.isEmpty())
        return;

    QString msgText;
    if (tracks.size() == 1) {
        msgText = QString("%1: Max Volume has been reached — "
                          "your audio is in danger of clipping and distorting.")
                  .arg(tracks.first());
    } else {
        msgText = QString("%1: Max Volume has been reached — "
                          "your audio is in danger of clipping and distorting.")
                  .arg(tracks.join(", "));
    }

    QMessageBox *msg = new QMessageBox(this);
    msg->setWindowTitle("Clipping Warning");
    msg->setText(msgText);
    msg->setIcon(QMessageBox::Warning);
    msg->setAttribute(Qt::WA_DeleteOnClose);
    msg->setStandardButtons(QMessageBox::Ok);
    msg->show();
}

void AudioMeterWidget::onTimer()
{
// Decrement clip cooldown timer
    if (m_clipCooldown > 0)
        m_clipCooldown--;

    bool anyClipped = false;

    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        if (!m_trackEnabled[i])
            continue;

        m_displayPeak[i] = m_peak[i].load();

        // Check for clip — accumulate, don't pop yet
        bool clipped = m_clipped[i].exchange(false);
        if (clipped) {
            m_pendingClip[i] = true;
            anyClipped = true;
        }
        // Peak hold
        float hold = m_hold[i].load();
        if (hold > m_displayHold[i]) {
            m_displayHold[i] = hold;
            m_holdTimer[i]   = PEAK_HOLD_TICKS;
        } else {
            if (m_holdTimer[i] > 0) {
                m_holdTimer[i]--;
            } else {
                m_displayHold[i] -= 0.5f;
                if (m_displayHold[i] < DB_MIN)
                    m_displayHold[i] = DB_MIN;
                m_hold[i].store(m_displayHold[i]);
		}
        }
    }

    // Fire batched warning, then start 10-second cooldown (30fps * 10 = 300)
    if (anyClipped && m_clipCooldown == 0) {
        showClipWarning();
        m_clipCooldown = 300;
    }

    update();
}

void AudioMeterWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();

    p.fillRect(0, 0, w, h, QColor(15, 15, 15));

const int labelWidth   = 44;
    const int targetWidth  = 36;  // space for Min/Max labels left of dB scale
    const int leftPad      = labelWidth + targetWidth;
    const int meterHeight  = h - 30;
    const int topPad       = 10;
    const int trackGap     = 5;

    // Count enabled tracks
    int enabledCount = 0;
    for (int i = 0; i < MAX_AUDIO_MIXES; i++)
        if (m_trackEnabled[i]) enabledCount++;

    if (enabledCount == 0)
        return;

const int totalMeterWidth = w - leftPad;
    const int barWidth = (totalMeterWidth - (enabledCount - 1) * trackGap)
                         / enabledCount;

    // Scale markings on left
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

  // Target labels in the leftmost gutter — bold, larger
    {
        float posMin = dbToPos(TARGET_MIN_DB);
        float posMax = dbToPos(TARGET_MAX_DB);
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
        if (!m_trackEnabled[i])
            continue;

        float peak = m_displayPeak[i];
        float hold = m_displayHold[i];

        int peakH  = (int)(dbToPos(peak) * meterHeight);
        int holdY  = topPad + (int)((1.0f - dbToPos(hold)) * meterHeight);

        // Draw background gradient (dark, unlit portion)
        for (int y = topPad; y < topPad + meterHeight; y++) {
            float db = DB_MIN + (1.0f - (float)(y - topPad) / meterHeight)
                       * (DB_MAX - DB_MIN);
            QColor c = colorAtDb(db, 0.18f);
            p.fillRect(trackX, y, barWidth, 1, c);
        }

        // Draw active gradient (lit portion)
        if (peakH > 0) {
            int barTop = topPad + meterHeight - peakH;
            for (int y = barTop; y < topPad + meterHeight; y++) {
                float db = DB_MIN + (1.0f - (float)(y - topPad) / meterHeight)
                           * (DB_MAX - DB_MIN);
                QColor c = colorAtDb(db, 1.0f);
                p.fillRect(trackX, y, barWidth, 1, c);
            }
        }

        // Peak hold line — colour matches the hold level
        if (hold > DB_MIN) {
            QColor holdColour = colorAtDb(hold, 1.0f);
            p.fillRect(trackX, holdY, barWidth, 2, holdColour);
        }

// Track label
        p.setPen(QColor(200, 200, 200));
        QFont trackFont = p.font();
        trackFont.setBold(true);
        trackFont.setPointSize(8);
        p.setFont(trackFont);
        p.drawText(trackX, topPad + meterHeight + 4,
                   barWidth, 20,
                   Qt::AlignHCenter | Qt::AlignTop,
                   QString("T%1").arg(i + 1));

        trackX += barWidth + trackGap;
    }

    // Draw target lines ON TOP of the meters
    auto drawTargetLine = [&](float db, QColor colour) {
        float pos = dbToPos(db);
        int   y   = topPad + (int)((1.0f - pos) * meterHeight);
        p.setPen(QPen(colour, 2, Qt::DashLine));
        p.drawLine(leftPad, y, w, y);
    };
    drawTargetLine(TARGET_MIN_DB, QColor(100, 220, 100, 220));
    drawTargetLine(TARGET_MAX_DB, QColor(220, 180, 50, 220));
}

// ---- Settings Dialog ----

SettingsDialog::SettingsDialog(AudioMeterWidget *meter, QWidget *parent)
    : QDialog(parent), m_meter(meter)
{
    setWindowTitle("Audio Meter Settings");
    setMinimumWidth(200);

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *label = new QLabel("Display Tracks:", this);
    layout->addWidget(label);

    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        m_checkboxes[i] = new QCheckBox(QString("Track %1").arg(i + 1), this);
        m_checkboxes[i]->setChecked(meter->isTrackEnabled(i));

        // Use index capture by value to avoid dangling refs
        int trackIndex = i;
        connect(m_checkboxes[i], &QCheckBox::toggled,
                this, [this, trackIndex](bool checked) {
                    if (m_meter)
                        m_meter->setTrackEnabled(trackIndex, checked);
                });

        layout->addWidget(m_checkboxes[i]);
    }

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);

    setLayout(layout);
}