#pragma once

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QWidget>
#include <QDialog>
#include <QTimer>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
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

// Default target line positions — used on first run / if config is missing.
// These are NOT the live values; the widget stores those as member variables
// so they can be changed at runtime without recompiling.
#define TARGET_MIN_DB_DEFAULT  -12.0f
#define TARGET_MAX_DB_DEFAULT   -3.0f

// OBS config section and key names
#define CONFIG_SECTION          "AudioTrackMeters"
#define CONFIG_KEY_TARGET_MIN   "TargetMinDb"
#define CONFIG_KEY_TARGET_MAX   "TargetMaxDb"

class AudioMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit AudioMeterWidget(QWidget *parent = nullptr);
    ~AudioMeterWidget() override;

    // Called from audio thread — must be lock-free
    void setLevel(int track, float dbfs);

    // Called from audio thread (isTrackEnabled) AND GUI thread (setTrackEnabled)
    // Must be atomic.
    bool isTrackEnabled(int track) const;
    void setTrackEnabled(int track, bool enabled);

    // Target line accessors — GUI thread only
    float targetMinDb() const { return m_targetMinDb; }
    float targetMaxDb() const { return m_targetMaxDb; }
    void  setTargetMinDb(float db);
    void  setTargetMaxDb(float db);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onTimer();

private:
    void   showClipWarning();
    float  dbToPos(float db) const;
    QColor colorAtDb(float db, float brightness) const;
    void   saveConfig() const;

    QTimer *m_timer = nullptr;

    // ── Audio-thread writers, GUI-thread readers ───────────────────────────
    // All must be std::atomic<>. A plain bool or float with cross-thread
    // access is undefined behaviour in C++ even on x86.
    std::array<std::atomic<float>, MAX_AUDIO_MIXES> m_peak;
    std::array<std::atomic<bool>,  MAX_AUDIO_MIXES> m_clipped;

    // m_hold: audio thread does atomic-max via compare_exchange_weak;
    //         GUI thread does exchange(DB_MIN) to read-and-clear.
    std::array<std::atomic<float>, MAX_AUDIO_MIXES> m_hold;

    // m_trackEnabled: GUI thread writes (setTrackEnabled),
    //                 audio thread reads (isTrackEnabled).
    // MUST be atomic — plain bool with cross-thread access is undefined behaviour.
    std::array<std::atomic<bool>, MAX_AUDIO_MIXES> m_trackEnabled;

    // ── GUI-thread only ───────────────────────────────────────────────────
    // Only ever touched inside onTimer() or paintEvent(), both on the GUI thread.
    std::array<float, MAX_AUDIO_MIXES> m_displayPeak;
    std::array<float, MAX_AUDIO_MIXES> m_displayHold;
    std::array<int,   MAX_AUDIO_MIXES> m_holdTimer;
    std::array<bool,  MAX_AUDIO_MIXES> m_pendingClip;
    int   m_clipCooldown = 0;

    // Target line positions — GUI thread only, persisted to OBS user config
    float m_targetMinDb = TARGET_MIN_DB_DEFAULT;
    float m_targetMaxDb = TARGET_MAX_DB_DEFAULT;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(AudioMeterWidget *meter, QWidget *parent = nullptr);

private:
    void validateTargets();

    AudioMeterWidget *m_meter;
    std::array<QCheckBox *, MAX_AUDIO_MIXES> m_checkboxes;
    QDoubleSpinBox *m_minSpinBox = nullptr;
    QDoubleSpinBox *m_maxSpinBox = nullptr;
};
