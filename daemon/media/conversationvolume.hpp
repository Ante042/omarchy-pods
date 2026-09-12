#pragma once

#include <QDebug>
#include <QProcess>
#include <QVariantAnimation>

class ConversationVolume : public QObject
{
public:
    explicit ConversationVolume(QObject *parent = nullptr) : QObject(parent)
    {
        m_fade.setEasingCurve(QEasingCurve::InOutSine);
        connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            if (m_fade.state() != QAbstractAnimation::Running)
                return;
            if (send(value.toDouble()))
                m_gain = value.toDouble();
            else
                m_fade.stop();
        });
    }

    ~ConversationVolume() override { reset(); }

    void setSpeaking(bool speaking, const QString &sink)
    {
        if (sink != m_sink) {
            reset();
            m_sink = sink;
        }
        // PulseAudio's 20% volume is cubic, so the corresponding software gain is 0.2 cubed.
        constexpr double conversationGain = 0.008;
        const double target = speaking ? conversationGain : 1.0;
        if (m_sink.isEmpty() || (m_fade.state() == QAbstractAnimation::Running
            && m_fade.endValue().toDouble() == target))
            return;
        m_fade.stop();
        if (m_gain == target)
            return;
        m_fade.setStartValue(m_gain);
        m_fade.setEndValue(target);
        constexpr int fadeDownMs = 600;
        constexpr int fadeUpMs = 1000;
        m_fade.setDuration(speaking ? fadeDownMs : fadeUpMs);
        m_fade.start();
    }

    void reset()
    {
        m_fade.stop();
        if (!m_sink.isEmpty())
            send(1.0);
        m_sink.clear();
        m_gain = 1.0;
    }

private:
    bool send(double gain)
    {
        // Software gain leaves Bluetooth's coarse hardware volume and the user's volume setting alone.
        const QString parameters = QStringLiteral("{ volume: %1 }").arg(gain, 0, 'f', 6);
        QProcess command;
        command.start(QStringLiteral("pw-cli"), {QStringLiteral("set-param"), m_sink,
                                               QStringLiteral("Props"), parameters});
        constexpr int commandTimeoutMs = 2000;
        const bool finished = command.waitForFinished(commandTimeoutMs);
        const QByteArray error = command.readAllStandardError();
        if (!finished || command.exitStatus() != QProcess::NormalExit
            || command.exitCode() != 0 || error.contains("Error:")) {
            qWarning() << "PipeWire conversation volume failed for" << m_sink
                       << command.errorString() << error;
            return false;
        }
        return true;
    }

    QVariantAnimation m_fade;
    QString m_sink;
    double m_gain = 1.0;
};
