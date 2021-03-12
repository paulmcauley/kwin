/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_inputeventfilter.h"
#include "drm_backend.h"
#include "wayland_server.h"

#include <QApplication>
#include <QKeyEvent>

#include <KWaylandServer/seat_interface.h>

namespace KWin
{

DpmsInputEventFilter::DpmsInputEventFilter(DrmBackend *backend)
    : InputEventFilter()
    , m_backend(backend)
{
}

DpmsInputEventFilter::~DpmsInputEventFilter() = default;

bool DpmsInputEventFilter::pointerEvent(QMouseEvent *event, quint32 nativeButton)
{
    Q_UNUSED(event)
    Q_UNUSED(nativeButton)
    notify();
    return true;
}

bool DpmsInputEventFilter::wheelEvent(QWheelEvent *event)
{
    Q_UNUSED(event)
    notify();
    return true;
}

bool DpmsInputEventFilter::keyEvent(QKeyEvent *event)
{
    if (event->type() == QKeyEvent::KeyPress) {
        notify();
    }
    return true;
}

bool DpmsInputEventFilter::touchDown(qint32 id, const QPointF &pos, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(pos)
    Q_UNUSED(time)
    return true;
}

bool DpmsInputEventFilter::touchUp(qint32 id, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(time)
    return true;
}

bool DpmsInputEventFilter::touchMotion(qint32 id, const QPointF &pos, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(pos)
    Q_UNUSED(time)
    // ignore the event
    return true;
}

void DpmsInputEventFilter::notify()
{
    // queued to not modify the list of event filters while filtering
    QMetaObject::invokeMethod(m_backend, &DrmBackend::turnOutputsOn, Qt::QueuedConnection);
}

}
