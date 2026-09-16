// SPDX-License-Identifier: MIT
#ifndef SYNAPSE_SETTINGS_GUI_THEME_H
#define SYNAPSE_SETTINGS_GUI_THEME_H

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class SettingsTheme final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString id READ id NOTIFY paletteChanged)
  Q_PROPERTY(QColor background READ background NOTIFY paletteChanged)
  Q_PROPERTY(QColor surface READ surface NOTIFY paletteChanged)
  Q_PROPERTY(QColor surfaceHover READ surfaceHover NOTIFY paletteChanged)
  Q_PROPERTY(QColor border READ border NOTIFY paletteChanged)
  Q_PROPERTY(QColor accent READ accent NOTIFY paletteChanged)
  Q_PROPERTY(QColor text READ text NOTIFY paletteChanged)
  Q_PROPERTY(QColor muted READ muted NOTIFY paletteChanged)
  Q_PROPERTY(QColor urgent READ urgent NOTIFY paletteChanged)
  Q_PROPERTY(QColor accentSoft READ accentSoft NOTIFY paletteChanged)
  Q_PROPERTY(QColor onAccent READ onAccent NOTIFY paletteChanged)
  Q_PROPERTY(QColor onUrgent READ onUrgent NOTIFY paletteChanged)
  Q_PROPERTY(bool providerValid READ providerValid NOTIFY statusChanged)
  Q_PROPERTY(QString reasonId READ reasonId NOTIFY statusChanged)
  Q_PROPERTY(quint64 revision READ revision NOTIFY paletteChanged)

public:
  struct Palette {
    QString id;
    QColor background;
    QColor surface;
    QColor surfaceHover;
    QColor border;
    QColor accent;
    QColor text;
    QColor muted;
    QColor urgent;
  };

  explicit SettingsTheme(QString providerPath, QObject *parent = nullptr);

  QString id() const;
  QColor background() const;
  QColor surface() const;
  QColor surfaceHover() const;
  QColor border() const;
  QColor accent() const;
  QColor text() const;
  QColor muted() const;
  QColor urgent() const;
  QColor accentSoft() const;
  QColor onAccent() const;
  QColor onUrgent() const;
  bool providerValid() const;
  QString reasonId() const;
  quint64 revision() const;

  bool reload();
  void watchConfig(const QString &path, const QStringList &statePaths = {});

signals:
  void paletteChanged();
  void statusChanged();

private:
  static bool parsePayload(const QByteArray &payload, Palette *palette,
                           QString *reasonId);
  static QColor contrastingText(const QColor &color);
  static QColor translucent(const QColor &color, int alpha);
  void setFailure(const QString &reasonId);
  void scheduleReload();
  void refreshWatchPaths();

  QString providerPath_;
  QStringList watchPaths_;
  Palette palette_;
  bool providerValid_ = false;
  QString reasonId_ = QStringLiteral("provider-unavailable");
  quint64 revision_ = 0;
  QFileSystemWatcher watcher_;
  QTimer reloadTimer_;
};

#endif
