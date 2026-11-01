#include "ui/historychart.h"

#include <QDateTime>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace fanctl {
namespace {

const QColor kCpu(0xE0, 0x6C, 0x3A);
const QColor kGpu(0x3A, 0x8F, 0xD0);

QString formatValue(double value, const QString &unit)
{
    if (unit == QLatin1String("rpm"))
        return QStringLiteral("%1").arg(qRound(value));
    return QStringLiteral("%1%2").arg(value, 0, 'f', 1).arg(unit);
}

}  // namespace

HistoryChart::HistoryChart(const QString &title, const QString &unit, double yMin, double yMax,
                           QWidget *parent)
    : QWidget(parent),
      m_title(title),
      m_unit(unit),
      m_yMin(yMin),
      m_yMax(yMax),
      m_cpuColor(kCpu),
      m_gpuColor(kGpu)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    setMinimumHeight(150);
}

QSize HistoryChart::sizeHint() const
{
    return QSize(420, 170);
}

void HistoryChart::setRetentionSeconds(int seconds)
{
    m_retentionSeconds = std::max(10, seconds);
    update();
}

void HistoryChart::reset()
{
    m_samples.clear();
    update();
}

void HistoryChart::append(qint64 epochMs, double cpu, double gpu)
{
    Sample sample;
    sample.ms = epochMs;
    sample.cpu = cpu > -273.0 ? std::optional<double>(cpu) : std::nullopt;
    sample.gpu = gpu > -273.0 ? std::optional<double>(gpu) : std::nullopt;
    m_samples.append(sample);
    trimTo(epochMs);
    update();
}

void HistoryChart::trimTo(qint64 newestMs)
{
    const qint64 horizon = newestMs - qint64(m_retentionSeconds) * 1000;
    while (!m_samples.isEmpty() && m_samples.first().ms < horizon)
        m_samples.removeFirst();
}

double HistoryChart::autoscaleMax() const
{
    double peak = 0.0;
    for (const Sample &sample : m_samples) {
        if (sample.cpu)
            peak = std::max(peak, *sample.cpu);
        if (sample.gpu)
            peak = std::max(peak, *sample.gpu);
    }

    const double step = m_unit == QLatin1String("rpm") ? 500.0 : 10.0;
    return std::max(step, std::ceil(peak * 1.15 / step) * step);
}

std::optional<double> HistoryChart::lastVisible(bool gpu) const
{
    for (auto it = m_samples.crbegin(); it != m_samples.crend(); ++it) {
        const std::optional<double> &value = gpu ? it->gpu : it->cpu;
        if (value)
            return value;
    }
    return std::nullopt;
}

void HistoryChart::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF area = contentsRect();
    const QRectF plot = area.adjusted(44, 26, -12, -20);

    QPalette pal = palette();
    painter.fillRect(area, pal.color(QPalette::Base));
    painter.setPen(pal.color(QPalette::WindowText));

    double yMin = m_yMin;
    double yMax = m_yMax;
    if (yMin < 0.0)
        yMin = 0.0;
    if (yMax <= yMin)
        yMax = autoscaleMax();

    QFont titleFont = font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(QPointF(area.left() + 6, area.top() + 16), m_title);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 start = now - qint64(m_retentionSeconds) * 1000;

    // Legend with the most recent values.
    QFont smallFont = font();
    smallFont.setBold(false);
    smallFont.setPointSizeF(smallFont.pointSizeF() * 0.9);
    painter.setFont(smallFont);

    int legendRight = int(plot.right());
    const auto drawLegend = [&](const QColor &color, const QString &name, bool gpu) {
        const std::optional<double> value = lastVisible(gpu);
        const QString text = QStringLiteral("%1 %2")
                                 .arg(name, value ? formatValue(*value, m_unit) : QStringLiteral("--"));
        const int textWidth = QFontMetrics(smallFont).horizontalAdvance(text);
        painter.setPen(color);
        painter.drawText(QPointF(legendRight - textWidth, area.top() + 16), text);
        legendRight -= textWidth + 18;
        painter.setPen(QPen(color, 3));
        painter.drawLine(QPointF(legendRight, area.top() + 12), QPointF(legendRight + 12, area.top() + 12));
        legendRight -= 20;
        painter.setPen(pal.color(QPalette::WindowText));
    };
    drawLegend(kGpu, QStringLiteral("GPU"), true);
    drawLegend(kCpu, QStringLiteral("CPU"), false);

    // Grid and y labels.
    painter.setPen(QPen(pal.color(QPalette::Midlight), 1, Qt::DotLine));
    QFont axisFont = smallFont;
    const int rows = 4;
    for (int i = 0; i <= rows; ++i) {
        const double ratio = double(i) / rows;
        const double y = plot.top() + plot.height() * (1.0 - ratio);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        const double value = yMin + (yMax - yMin) * ratio;
        painter.setFont(axisFont);
        painter.setPen(pal.color(QPalette::Dark));
        painter.drawText(QRectF(0, y - 8, plot.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(qRound(value)));
        painter.setPen(QPen(pal.color(QPalette::Midlight), 1, Qt::DotLine));
    }

    // x labels every minute.
    painter.setPen(pal.color(QPalette::Dark));
    for (int secondsAgo = m_retentionSeconds; secondsAgo >= 0; secondsAgo -= 60) {
        const double ratio = 1.0 - double(secondsAgo) / m_retentionSeconds;
        const double x = plot.left() + plot.width() * ratio;
        const QString label = secondsAgo == 0 ? QStringLiteral("now")
                                              : QStringLiteral("-%1m").arg(secondsAgo / 60);
        painter.drawText(QRectF(x - 22, plot.bottom() + 2, 44, 16), Qt::AlignHCenter | Qt::AlignTop, label);
    }

    auto seriesPath = [&](bool gpu) {
        QPainterPath path;
        bool pen_up = true;
        for (const Sample &sample : m_samples) {
            const std::optional<double> &value = gpu ? sample.gpu : sample.cpu;
            if (sample.ms < start)
                continue;
            if (!value) {
                pen_up = true;
                continue;
            }
            const double x = plot.left()
                             + plot.width() * double(sample.ms - start) / double(now - start);
            const double clamped = std::clamp(*value, yMin, yMax);
            const double y = plot.bottom()
                             - plot.height() * (clamped - yMin) / std::max(1e-6, yMax - yMin);
            if (pen_up) {
                path.moveTo(x, y);
                pen_up = false;
            } else {
                path.lineTo(x, y);
            }
        }
        return path;
    };

    if (m_samples.isEmpty()) {
        painter.setPen(pal.color(QPalette::PlaceholderText));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("Collecting data…"));
        return;
    }

    painter.setClipRect(plot);
    painter.setPen(QPen(m_cpuColor, 2));
    painter.drawPath(seriesPath(false));
    painter.setPen(QPen(m_gpuColor, 2));
    painter.drawPath(seriesPath(true));
    painter.setClipping(false);
}

}  // namespace fanctl
