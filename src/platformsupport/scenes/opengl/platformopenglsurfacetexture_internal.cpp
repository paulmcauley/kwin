/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "platformopenglsurfacetexture_internal.h"

namespace KWin
{

PlatformOpenGLSurfaceTextureInternal::PlatformOpenGLSurfaceTextureInternal(OpenGLBackend *backend,
                                                                           SurfaceTextureInternal *pixmap)
    : PlatformOpenGLSurfaceTexture(backend)
    , m_pixmap(pixmap)
{
}

} // namespace KWin
