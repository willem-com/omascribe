#pragma once

#include <QString>

class Document;

bool exportNotePdf(const Document *doc, const QString &path);
bool exportNoteSvg(const Document *doc, const QString &path);
QString suggestedExportName(const Document *doc);
