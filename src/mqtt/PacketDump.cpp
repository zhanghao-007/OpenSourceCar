#include "PacketDump.h"

namespace Mqtt {

QString packetTypeName(int type) {
    switch (type) {
    case PACKET_CONNECT:     return QStringLiteral("CONNECT");
    case PACKET_CONNACK:     return QStringLiteral("CONNACK");
    case PACKET_PUBLISH:     return QStringLiteral("PUBLISH");
    case PACKET_PUBACK:      return QStringLiteral("PUBACK");
    case PACKET_PUBREC:      return QStringLiteral("PUBREC");
    case PACKET_PUBREL:      return QStringLiteral("PUBREL");
    case PACKET_PUBCOMP:     return QStringLiteral("PUBCOMP");
    case PACKET_SUBSCRIBE:   return QStringLiteral("SUBSCRIBE");
    case PACKET_SUBACK:      return QStringLiteral("SUBACK");
    case PACKET_UNSUBSCRIBE: return QStringLiteral("UNSUBSCRIBE");
    case PACKET_UNSUBACK:    return QStringLiteral("UNSUBACK");
    case PACKET_PINGREQ:     return QStringLiteral("PINGREQ");
    case PACKET_PINGRESP:    return QStringLiteral("PINGRESP");
    case PACKET_DISCONNECT:  return QStringLiteral("DISCONNECT");
    default:                return QStringLiteral("UNKNOWN(%1)").arg(type);
    }
}

QString packetFlagsText(int type, quint8 flags) {
    switch (type) {
    case PACKET_PUBLISH:
        return QStringLiteral("DUP=%1 QoS=%2 RETAIN=%3")
            .arg((flags & 0x08) ? 1 : 0)
            .arg((flags & 0x06) >> 1)
            .arg((flags & 0x01) ? 1 : 0);
    case PACKET_PUBREL:
    case PACKET_SUBSCRIBE:
    case PACKET_UNSUBSCRIBE:
        // 规范要求这几个报文的标志位必须是 0x02，显示实际值便于发现固件发错
        return QStringLiteral("FLAGS=0x%1%2")
            .arg(flags, 1, 16, QChar('0'))
            .arg(flags == 0x02 ? QString() : QStringLiteral(" (应为0x02)"));
    default:
        return QStringLiteral("FLAGS=0x%1").arg(flags, 1, 16, QChar('0'));
    }
}

QString hexDump(const QByteArray &data, int maxBytes, int perLine) {
    if (data.isEmpty()) {
        return QStringLiteral("(空报文)");
    }

    QString result;
    int size = data.size();
    int bytesToDump = qMin(size, maxBytes);

    for (int i = 0; i < bytesToDump; i += perLine) {
        QString hexLine;
        QString asciiLine;
        for (int j = 0; j < perLine; ++j) {
            if (i + j < bytesToDump) {
                quint8 byte = static_cast<quint8>(data.at(i + j));
                hexLine += QStringLiteral("%1 ").arg(byte, 2, 16, QChar('0'));
                asciiLine += (byte >= 0x20 && byte <= 0x7E) ? QChar(byte) : QChar('.');
            } else {
                hexLine += QStringLiteral("   ");
                asciiLine += QChar(' ');
            }
            if ((j + 1) % 8 == 0 && j < perLine - 1) {
                hexLine += QStringLiteral(" ");
            }
        }
        result += QStringLiteral("%1  %2 |%3|\n")
            .arg(i, 4, 16, QChar('0'))
            .arg(hexLine.leftJustified(perLine * 3 + (perLine / 8), QChar(' ')))
            .arg(asciiLine);
    }

    if (size > maxBytes) {
        result += QStringLiteral("… （共 %1 字节，仅显示前 %2 字节）\n")
            .arg(size)
            .arg(maxBytes);
    }

    while (result.endsWith(QLatin1Char('\n')))
        result.chop(1);
    return result;
}

QString hexSummary(const QByteArray &data, int maxBytes) {
    if (data.isEmpty()) {
        return QStringLiteral("(空报文)");
    }

    QString result;
    int size = data.size();
    int bytesToDump = qMin(size, maxBytes);

    for (int i = 0; i < bytesToDump; ++i) {
        quint8 byte = static_cast<quint8>(data.at(i));
        result += QStringLiteral("%1 ").arg(byte, 2, 16, QChar('0'));
    }

    if (size > maxBytes) {
        result += QStringLiteral("…");
    }

    result += QStringLiteral("（共 %1 字节）").arg(size);
    return result;
}

int typeOf(quint8 header) {
    return header >> 4;
}

quint8 flagsOf(quint8 header) {
    return header & 0x0F;
}

} // namespace Mqtt
