#pragma once

#include <QString>

class Document;

bool exportNotePdf(const Document *doc, const QString &path);
bool exportNoteSvg(const Document *doc, const QString &path);
bool exportNotePng(const Document *doc, const QString &path);
bool writeAgentReadout(const Document *doc, const QString &dir);
QString suggestedExportName(const Document *doc);
