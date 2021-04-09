/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "platformqpaintersurfacetexture_wayland.h"
#include "surfaceitem_wayland.h"

#include <KWaylandServer/buffer_interface.h>

namespace KWin
{

PlatformQPainterSurfaceTextureWayland::PlatformQPainterSurfaceTextureWayland(QPainterBackend *backend,
                                                                             SurfaceTextureWayland *texture)
    : PlatformQPainterSurfaceTexture(backend)
    , m_texture(texture)
{
}

bool PlatformQPainterSurfaceTextureWayland::create()
{
    KWaylandServer::BufferInterface *buffer = m_texture->buffer();
    if (Q_LIKELY(buffer)) {
        m_image = buffer->data().copy();
    }
    return !m_image.isNull();
}

void PlatformQPainterSurfaceTextureWayland::update(const QRegion &region)
{
    Q_UNUSED(region)
    KWaylandServer::BufferInterface *buffer = m_texture->buffer();
    if (Q_LIKELY(buffer)) {
        m_image = buffer->data().copy();
    }
}

} // namespace KWin
