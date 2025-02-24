/*
 *  SPDX-FileCopyrightText: 2009 Cyrille Berger <cberger@cberger.net>
 *  SPDX-FileCopyrightText: 2014 Sven Langkamp <sven.langkamp@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_MIRROR_MANAGER_H
#define KIS_MIRROR_MANAGER_H

#include <QObject>
#include <QPointer>
#include <kis_types.h>

#include "KisView.h"

class KisViewManager;
class KisKActionCollection;
class KisMirrorAxis;
class KisMirrorAxisConfig;

class KisMirrorManager : public QObject
{
    Q_OBJECT

public:
    KisMirrorManager(KisViewManager* view);
    ~KisMirrorManager() override;

    void setup(KisKActionCollection* collection);
    void setView(QPointer<KisView> imageView);

private Q_SLOTS:
    void updateAction();
    void slotSyncActionStates(bool val);
    void slotDocumentConfigChanged();
    void slotMirrorAxisConfigChanged();

private:
    QPointer<KisView> m_imageView;
    QAction *m_mirrorCanvas {nullptr};
    QAction *m_mirrorCanvasAroundCursor {nullptr};
    void setDecorationConfig();
    KisMirrorAxisSP decoration() const;
};

#endif // KIS_MIRROR_MANAGER_H
