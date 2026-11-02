#include "ui/historychart.h"

#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace fanctl {
namespace {

const QColor kCpu(0xE0, 0x6C, 0x3A);
const QColor kGpu(0x3A, 0x8F, 0xD0);

constexpr int kSidePad = 10;
constexpr int kTickPad = 18;
constexpr int kRightPad = 22;
constexpr double kMinLabelSpacing = 56.0;
constexpr int kLegendSwatch = 14;

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
    setMinimumHeight(120);
}

QSize HistoryChart::sizeHint() const
{
    return QSize(420, 160);
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
    const QPalette pal = palette();

    painter.setPen(QPen(pal.color(QPalette::Midlight), 1));
    painter.setBrush(pal.color(QPalette::Base));
    painter.drawRoundedRect(area.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);

    double yMin = m_yMin;
    double yMax = m_yMax;
    if (yMin < 0.0)
        yMin = 0.0;
    if (yMax <= yMin)
        yMax = autoscaleMax();

    QFont titleFont = font();
    titleFont.setBold(true);
    QFont smallFont = font();
    smallFont.setPointSizeF(smallFont.pointSizeF() * 0.9);

    constexpr int rows = 4;
    QFontMetrics axisMetrics(smallFont);
    QStringList ticks;
    int tickWidth = 0;
    for (int i = 0; i <= rows; ++i) {
        const QString tick = QString::number(qRound(yMin + (yMax - yMin) * double(i) / rows));
        tickWidth = std::max(tickWidth, axisMetrics.horizontalAdvance(tick));
        ticks << tick;
    }

    const int header = QFontMetrics(titleFont).height() + 14;
    const int footer = axisMetrics.height() + 8;
    const QRectF plot = area.adjusted(tickWidth + kTickPad, header, -kRightPad, -footer);

    struct Entry {
        QColor color;
        QString text;
        int width;
    };
    const auto legendText = [&](const QColor &color, const QString &name, bool gpu) {
        const std::optional<double> value = lastVisible(gpu);
        const QString text = QStringLiteral("%1 %2")
                                 .arg(name, value ? formatValue(*value, m_unit) : QStringLiteral("--"));
        return Entry{color, text, QFontMetrics(smallFont).horizontalAdvance(text)};
    };
    const QVector<Entry> entries = {legendText(m_cpuColor, QStringLiteral("CPU"), false),
                                    legendText(m_gpuColor, QStringLiteral("GPU"), true)};

    int legendWidth = 0;
    for (const Entry &entry : entries)
        legendWidth += entry.width + kLegendSwatch;
    legendWidth += kSidePad * (entries.size() - 1);

    // Title on the left, latest readings on the right, sharing one band. The
    // legend keeps its room and the title gives way, because the numbers are
    // the reason anyone reads this chart.
    const double legendLeft = std::max(plot.left(), plot.right() - legendWidth + kSidePad);
    const int titleRoom = int(legendLeft - area.left() - kSidePad * 2);
    painter.setFont(titleFont);
    painter.setPen(pal.color(QPalette::WindowText));
    painter.drawText(QRectF(area.left() + kSidePad, area.top(), titleRoom, header),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(titleFont).elidedText(m_title, Qt::ElideRight, std::max(30, titleRoom)));

    painter.setFont(smallFont);
    double x = legendLeft;
    const double bandMid = area.top() + header / 2.0;
    for (const Entry &entry : entries) {
        painter.setPen(QPen(entry.color, 3));
        painter.drawLine(QPointF(x, bandMid), QPointF(x + kLegendSwatch - 4, bandMid));
        painter.setPen(entry.color);
        painter.drawText(QRectF(x + kLegendSwatch, area.top(), entry.width, header),
                         Qt::AlignLeft | Qt::AlignVCenter, entry.text);
        x += entry.width + kLegendSwatch + kSidePad;
    }

    // Grid and y labels.
    for (int i = 0; i <= rows; ++i) {
        const double ratio = double(i) / rows;
        const double y = plot.top() + plot.height() * (1.0 - ratio);
        painter.setPen(QPen(pal.color(QPalette::Midlight), 1, Qt::DotLine));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(pal.color(QPalette::Dark));
        painter.drawText(QRectF(area.left(), y - footer / 2.0, plot.left() - area.left() - 8, footer),
                         Qt::AlignRight | Qt::AlignVCenter, ticks.at(i));
    }

    // Minute marks, thinned out so they cannot run into each other.
    painter.setPen(pal.color(QPalette::Dark));
    int step = 60;
    while (step < m_retentionSeconds
           && plot.width() * double(step) / double(m_retentionSeconds) < kMinLabelSpacing)
        step *= 2;
    for (int secondsAgo = 0; secondsAgo <= m_retentionSeconds; secondsAgo += step) {
        const double ratio = 1.0 - double(secondsAgo) / m_retentionSeconds;
        const double mark = plot.left() + plot.width() * ratio;
        const QString label = secondsAgo == 0 ? QStringLiteral("now")
                                              : QStringLiteral("-%1m").arg(secondsAgo / 60);
        // The edge labels would otherwise hang off the panel.
        const double boxLeft = std::min(std::max(mark - 24.0, area.left() + 2.0), area.right() - 50.0);
        painter.drawText(QRectF(boxLeft, plot.bottom() + 4, 48, footer),
                         Qt::AlignHCenter | Qt::AlignTop, label);
    }

    if (m_samples.isEmpty()) {
        painter.setPen(pal.color(QPalette::PlaceholderText));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("Collecting data…"));
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 start = now - qint64(m_retentionSeconds) * 1000;
    const double span = std::max<qint64>(1, now - start);

    auto seriesPath = [&](bool gpu) {
        QPainterPath path;
        bool penUp = true;
        for (const Sample &sample : m_samples) {
            const std::optional<double> &value = gpu ? sample.gpu : sample.cpu;
            if (sample.ms < start)
                continue;
            if (!value) {
                penUp = true;
                continue;
            }
            const double px = plot.left() + plot.width() * double(sample.ms - start) / span;
            const double clamped = std::clamp(*value, yMin, yMax);
            const double py = plot.bottom()
                              - plot.height() * (clamped - yMin) / std::max(1e-6, yMax - yMin);
            if (penUp) {
                path.moveTo(px, py);
                penUp = false;
            } else {
                path.lineTo(px, py);
            }
        }
        return path;
    };

    painter.setClipRect(plot);
    painter.setPen(QPen(m_cpuColor, 2));
    painter.drawPath(seriesPath(false));
    painter.setPen(QPen(m_gpuColor, 2));
    painter.drawPath(seriesPath(true));
    painter.setClipping(false);
}

}  // namespace fanctl
