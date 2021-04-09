/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "surfaceitem_internal.h"
#include "composite.h"
#include "scene.h"

namespace KWin
{

SurfaceItemInternal::SurfaceItemInternal(Scene::Window *window, Item *parent)
    : SurfaceItem(window, parent)
{
    Toplevel *toplevel = window->window();

    connect(toplevel, &Toplevel::bufferGeometryChanged,
            this, &SurfaceItemInternal::handleBufferGeometryChanged);

    setSize(toplevel->bufferGeometry().size());
}

QPointF SurfaceItemInternal::mapToBuffer(const QPointF &point) const
{
    return point * window()->window()->bufferScale();
}

QRegion SurfaceItemInternal::shape() const
{
    return QRegion(0, 0, width(), height());
}

SurfaceTexture *SurfaceItemInternal::createTexture()
{
    return new SurfaceTextureInternal(this);
}

void SurfaceItemInternal::handleBufferGeometryChanged(Toplevel *toplevel, const QRect &old)
{
    if (toplevel->bufferGeometry().size() != old.size()) {
        discardTexture();
    }
    setSize(toplevel->bufferGeometry().size());
}

SurfaceTextureInternal::SurfaceTextureInternal(SurfaceItemInternal *item, QObject *parent)
    : SurfaceTexture(Compositor::self()->scene()->createPlatformSurfaceTextureInternal(this), parent)
    , m_item(item)
{
}

QOpenGLFramebufferObject *SurfaceTextureInternal::fbo() const
{
    return m_fbo.data();
}

QImage SurfaceTextureInternal::image() const
{
    return m_rasterBuffer;
}

void SurfaceTextureInternal::create()
{
    update();
}

void SurfaceTextureInternal::update()
{
    const Toplevel *toplevel = m_item->window()->window();

    if (toplevel->internalFramebufferObject()) {
        m_fbo = toplevel->internalFramebufferObject();
        m_hasAlphaChannel = true;
    } else if (!toplevel->internalImageObject().isNull()) {
        m_rasterBuffer = toplevel->internalImageObject();
        m_hasAlphaChannel = m_rasterBuffer.hasAlphaChannel();
    }
}

bool SurfaceTextureInternal::isValid() const
{
    return !m_fbo.isNull() || !m_rasterBuffer.isNull();
}

} // namespace KWin
