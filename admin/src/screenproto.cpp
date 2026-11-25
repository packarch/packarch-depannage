#include "screenproto.hpp"

#include <QtNumeric>
#include <QtMath>

namespace {
constexpr int AXIS_MAX = 32767;
/// Une capture plus grosse n'en est pas une
constexpr qint64 MAX_FRAME = 32 * 1024 * 1024;
}  // namespace

namespace screen {

int pointerAxis(double pos, double length) {
    if (length <= 1)
        return 0;
    return qBound(0, qRound(pos / (length - 1) * AXIS_MAX), AXIS_MAX);
}

int evdevKey(quint32 nativeScanCode) {
    const qint64 code = static_cast<qint64>(nativeScanCode) - 8;
    return code >= 1 && code <= 248 ? static_cast<int>(code) : 0;
}

qint64 frameSize(const QByteArray& header) {
    if (!header.startsWith("F "))
        return -1;
    bool ok = false;
    const qint64 size = header.mid(2).toLongLong(&ok);
    return ok && size > 0 && size <= MAX_FRAME ? size : -1;
}

}  // namespace screen
