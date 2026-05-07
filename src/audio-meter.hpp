#pragma once

#include <QWidget>
#include <QDialog>
#include <QTimer>
#include <QCheckBox>
#include <QColor>
#include <atomic>
#include <array>

#define MAX_AUDIO_MIXES         6
#define PEAK_HOLD_TICKS         30
#define DB_MIN                 -60.0f
#define DB_MAX                   0.0f
#define THRESH_BRIGHT_GREEN    -24
#define THRESH_YELLOW          -12
#define THRESH_ORANGE           -6
#define THRESH_RED              -1
#define TARGET_MIN_DB          -12.0f
#define TARGET_MAX_DB           -3.0f

class AudioMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit AudioMeterWidget(QWidget *parent = nullptr);
    ~AudioMeterWidget() override;

    // Called from audio thread — must be lock-free
    void setLevel(int track, float dbfs);

    // Called from main thread
    bool isTrackEnabled(int track) const;
    void setTrackEnabled(int track, bool enabled);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onTimer();

private:
    void   showClipWarning();
    float  dbToPos(float db) const;
    QColor colorAtDb(float db, float brightness) const;

    QTimer *m_timer = nullptr;

    // Atomic peak values written from audio thread, read from UI thread
    std::array<std::atomic<float>, MAX_AUDIO_MIXES> m_peak;
    std::array<std::atomic<bool>,  MAX_AUDIO_MIXES> m_clipped;

    // UI-thread state
    std::array<float, MAX_AUDIO_MIXES> m_displayPeak;
    std::array<std::atomic<float>, MAX_AUDIO_MIXES> m_hold;
    std::array<float, MAX_AUDIO_MIXES> m_displayHold;
    std::array<int,   MAX_AUDIO_MIXES> m_holdTimer;
    std::array<bool,  MAX_AUDIO_MIXES> m_trackEnabled;

    // Clip warning state
    std::array<bool, MAX_AUDIO_MIXES> m_pendingClip;
    int m_clipCooldown = 0;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(AudioMeterWidget *meter, QWidget *parent = nullptr);

private:
    AudioMeterWidget *m_meter;
    std::array<QCheckBox *, MAX_AUDIO_MIXES> m_checkboxes;
};