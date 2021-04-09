/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "surfaceitem_wayland.h"
#include "composite.h"
#include "scene.h"

#include <KWaylandServer/buffer_interface.h>
#include <KWaylandServer/subcompositor_interface.h>
#include <KWaylandServer/surface_interface.h>

namespace KWin
{

SurfaceItemWayland::SurfaceItemWayland(KWaylandServer::SurfaceInterface *surface,
                                       Scene::Window *window, Item *parent)
    : SurfaceItem(window, parent)
    , m_surface(surface)
{
    connect(surface, &KWaylandServer::SurfaceInterface::surfaceToBufferMatrixChanged,
            this, &SurfaceItemWayland::discardQuads);
    connect(surface, &KWaylandServer::SurfaceInterface::surfaceToBufferMatrixChanged,
            this, &SurfaceItemWayland::discardTexture);

    connect(surface, &KWaylandServer::SurfaceInterface::sizeChanged,
            this, &SurfaceItemWayland::handleSurfaceSizeChanged);
    connect(surface, &KWaylandServer::SurfaceInterface::bufferSizeChanged,
            this, &SurfaceItemWayland::discardTexture);

    connect(surface, &KWaylandServer::SurfaceInterface::childSubSurfacesChanged,
            this, &SurfaceItemWayland::handleChildSubSurfacesChanged);
    connect(surface, &KWaylandServer::SurfaceInterface::committed,
            this, &SurfaceItemWayland::handleSurfaceCommitted);
    connect(surface, &KWaylandServer::SurfaceInterface::damaged,
            this, &SurfaceItemWayland::addDamage);
    connect(surface, &KWaylandServer::SurfaceInterface::childSubSurfaceAdded,
            this, &SurfaceItemWayland::handleChildSubSurfaceAdded);
    connect(surface, &KWaylandServer::SurfaceInterface::childSubSurfaceRemoved,
            this, &SurfaceItemWayland::handleChildSubSurfaceRemoved);

    KWaylandServer::SubSurfaceInterface *subsurface = surface->subSurface();
    if (subsurface) {
        connect(subsurface, &KWaylandServer::SubSurfaceInterface::positionChanged,
                this, &SurfaceItemWayland::handleSubSurfacePositionChanged);
        setPosition(subsurface->position());
    }

    const QList<KWaylandServer::SubSurfaceInterface *> children = surface->childSubSurfaces();
    for (KWaylandServer::SubSurfaceInterface *subsurface : children) {
        handleChildSubSurfaceAdded(subsurface);
    }

    setSize(surface->size());
}

QPointF SurfaceItemWayland::mapToBuffer(const QPointF &point) const
{
    if (m_surface) {
        return m_surface->mapToBuffer(point);
    }
    return point;
}

QRegion SurfaceItemWayland::shape() const
{
    return QRegion(0, 0, width(), height());
}

QRegion SurfaceItemWayland::opaque() const
{
    if (m_surface) {
        return m_surface->opaque();
    }
    return QRegion();
}

KWaylandServer::SurfaceInterface *SurfaceItemWayland::surface() const
{
    return m_surface;
}

void SurfaceItemWayland::handleSurfaceSizeChanged()
{
    setSize(m_surface->size());
}

void SurfaceItemWayland::handleSurfaceCommitted()
{
    if (m_surface->hasFrameCallbacks()) {
        scheduleRepaint();
    }
}

void SurfaceItemWayland::handleChildSubSurfaceAdded(KWaylandServer::SubSurfaceInterface *child)
{
    SurfaceItemWayland *subsurfaceItem = new SurfaceItemWayland(child->surface(), window());
    subsurfaceItem->setParent(this);
    subsurfaceItem->setParentItem(this);

    m_subsurfaces.insert(child, subsurfaceItem);
}

void SurfaceItemWayland::handleChildSubSurfaceRemoved(KWaylandServer::SubSurfaceInterface *child)
{
    delete m_subsurfaces.take(child);
}

void SurfaceItemWayland::handleChildSubSurfacesChanged()
{
    const QList<KWaylandServer::SubSurfaceInterface *> stackingOrder = m_surface->childSubSurfaces();
    QList<Item *> items;
    items.reserve(stackingOrder.count());

    for (KWaylandServer::SubSurfaceInterface *subsurface : stackingOrder) {
        items.append(m_subsurfaces[subsurface]);
    }

    stackChildren(items);
}

void SurfaceItemWayland::handleSubSurfacePositionChanged()
{
    setPosition(m_surface->subSurface()->position());
}

SurfaceTexture *SurfaceItemWayland::createTexture()
{
    return new SurfaceTextureWayland(this);
}

SurfaceTextureWayland::SurfaceTextureWayland(SurfaceItemWayland *item, QObject *parent)
    : SurfaceTexture(Compositor::self()->scene()->createPlatformSurfaceTextureWayland(this), parent)
    , m_item(item)
{
}

SurfaceTextureWayland::~SurfaceTextureWayland()
{
    setBuffer(nullptr);
}

KWaylandServer::SurfaceInterface *SurfaceTextureWayland::surface() const
{
    return m_item->surface();
}

KWaylandServer::BufferInterface *SurfaceTextureWayland::buffer() const
{
    return m_buffer;
}

void SurfaceTextureWayland::create()
{
    update();
}

void SurfaceTextureWayland::update()
{
    KWaylandServer::SurfaceInterface *surface = m_item->surface();
    if (surface) {
        setBuffer(surface->buffer());
    }
}

bool SurfaceTextureWayland::isValid() const
{
    // Referenced buffers get destroyed under our nose, check also the platform texture
    // to work around BufferInterface's weird api.
    return m_buffer || platformTexture()->isValid();
}

void SurfaceTextureWayland::clearBuffer()
{
    setBuffer(nullptr);
}

void SurfaceTextureWayland::setBuffer(KWaylandServer::BufferInterface *buffer)
{
    if (m_buffer == buffer) {
        return;
    }
    if (m_buffer) {
        disconnect(m_buffer, &KWaylandServer::BufferInterface::aboutToBeDestroyed,
                   this, &SurfaceTextureWayland::clearBuffer);
        m_buffer->unref();
    }
    m_buffer = buffer;
    if (m_buffer) {
        m_buffer->ref();
        connect(m_buffer, &KWaylandServer::BufferInterface::aboutToBeDestroyed,
                this, &SurfaceTextureWayland::clearBuffer);
        m_hasAlphaChannel = m_buffer->hasAlphaChannel();
    }
}

SurfaceItemXwayland::SurfaceItemXwayland(Scene::Window *window, Item *parent)
    : SurfaceItemWayland(window->window()->surface(), window, parent)
{
}

QRegion SurfaceItemXwayland::shape() const
{
    const Toplevel *toplevel = window()->window();
    if (window()->isShaded()) {
        return QRegion();
    }

    const QRect clipRect = toplevel->clientGeometry().translated(-toplevel->bufferGeometry().topLeft());
    const QRegion shape = toplevel->shapeRegion();

    return shape & clipRect;
}

} // namespace KWin
