/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "platformqpaintersurfacetexture_internal.h"
#include "surfaceitem_internal.h"

namespace KWin
{

PlatformQPainterSurfaceTextureInternal::PlatformQPainterSurfaceTextureInternal(QPainterBackend *backend,
                                                                               SurfaceTextureInternal *texture)
    : PlatformQPainterSurfaceTexture(backend)
    , m_texture(texture)
{
}

bool PlatformQPainterSurfaceTextureInternal::create()
{
    update(QRegion());
    return !m_image.isNull();
}

void PlatformQPainterSurfaceTextureInternal::update(const QRegion &region)
{
    Q_UNUSED(region)
    m_image = m_texture->image();
}

} // namespace KWin
