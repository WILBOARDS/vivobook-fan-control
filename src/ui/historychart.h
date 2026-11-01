#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <optional>

namespace fanctl {

// Two-line time series (CPU + GPU) painted with QPainter, so the package needs
// no Qt Charts dependency and keeps the GPL-only modules out of the picture.
class HistoryChart : public QWidget {
    Q_OBJECT

public:
    explicit HistoryChart(const QString &title, const QString &unit, double yMin = -1.0,
                          double yMax = -1.0, QWidget *parent = nullptr);

    void append(qint64 epochMs, double cpu, double gpu);
    void setRetentionSeconds(int seconds);
    void reset();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Sample {
        qint64 ms = 0;
        std::optional<double> cpu;
        std::optional<double> gpu;
    };

    void trimTo(qint64 newestMs);
    std::optional<double> lastVisible(bool gpu) const;
    double autoscaleMax() const;

    QString m_title;
    QString m_unit;
    double m_yMin;
    double m_yMax;
    int m_retentionSeconds = 300;
    QList<Sample> m_samples;
    QColor m_cpuColor;
    QColor m_gpuColor;
};

}  // namespace fanctl
