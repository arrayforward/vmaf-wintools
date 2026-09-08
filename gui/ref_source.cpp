#include "ref_source.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

const char* const kRefSourceName = "ai_game_1080p60.mp4";

QString resolveRefSource() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString name = QString::fromLatin1(kRefSourceName);
    const QStringList candidates = {appDir + "/" + name, appDir + "/testdata/" + name,
                                    appDir + "/../../testdata/" + name};
    for (const QString& p : candidates) {
        if (QFileInfo::exists(p)) return QDir(p).canonicalPath();
    }
    return QString();
}
