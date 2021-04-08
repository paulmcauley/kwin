/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2017 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_buffer_gbm.h"
#include "gbm_surface.h"

#include "logging.h"
#include "drm_gpu.h"

// system
#include <sys/mman.h>
// c++
#include <cerrno>
// drm
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
// KWaylandServer
#include "KWaylandServer/buffer_interface.h"
#include <drm_fourcc.h>

namespace KWin
{

GbmBuffer::GbmBuffer(const QSharedPointer<GbmSurface> &surface)
    : m_surface(surface)
{
    m_bo = m_surface->lockFrontBuffer();
    if (m_bo) {
        m_stride = gbm_bo_get_stride(m_bo);
    }
}

GbmBuffer::GbmBuffer(gbm_bo *buffer, KWaylandServer::BufferInterface *bufferInterface)
    : m_bo(buffer)
    , m_bufferInterface(bufferInterface)
{
    m_stride = gbm_bo_get_stride(m_bo);
    if (m_bufferInterface) {
        m_bufferInterface->ref();
        connect(m_bufferInterface, &KWaylandServer::BufferInterface::aboutToBeDestroyed, this, &GbmBuffer::clearBufferInterface);
    }
}

GbmBuffer::~GbmBuffer()
{
    if (m_bufferInterface) {
        clearBufferInterface();
    }
    if (m_data) {
        gbm_bo_unmap(m_bo, m_data);
    }
    if (m_surface) {
        m_surface->releaseBuffer(m_bo);
    } else if (m_bo) {
        gbm_bo_destroy(m_bo);
    }
}

bool GbmBuffer::map()
{
    if (m_data) {
        return true;
    }
    return gbm_bo_map(m_bo, 0, 0, gbm_bo_get_width(m_bo), gbm_bo_get_height(m_bo), GBM_BO_USE_WRITE, &m_stride, &m_data);
}

void GbmBuffer::clearBufferInterface()
{
    disconnect(m_bufferInterface, &KWaylandServer::BufferInterface::aboutToBeDestroyed, this, &DrmGbmBuffer::clearBufferInterface);
    m_bufferInterface->unref();
    m_bufferInterface = nullptr;
}

DrmGbmBuffer::DrmGbmBuffer(DrmGpu *gpu, const QSharedPointer<GbmSurface> &surface)
    : DrmBuffer(gpu), GbmBuffer(surface)
{
    if (!m_bo) {
        qCWarning(KWIN_DRM) << "Locking front buffer failed";
        return;
    }
    initialize();
}

DrmGbmBuffer::DrmGbmBuffer(DrmGpu *gpu, gbm_bo *buffer, KWaylandServer::BufferInterface *bufferInterface)
    : DrmBuffer(gpu), GbmBuffer(buffer, bufferInterface)
{
    initialize();
}

DrmGbmBuffer::~DrmGbmBuffer()
{
    if (m_bufferId) {
        drmModeRmFB(m_gpu->fd(), m_bufferId);
    }
}

void DrmGbmBuffer::initialize()
{
    m_size = QSize(gbm_bo_get_width(m_bo), gbm_bo_get_height(m_bo));
    uint32_t handles[4] = { };
    uint32_t strides[4] = { };
    uint32_t offsets[4] = { };
    uint64_t modifiers[4] = { };

    if (gbm_bo_get_handle_for_plane(m_bo, 0).s32 != -1) {
        for (int i = 0; i < gbm_bo_get_plane_count(m_bo); i++) {
            handles[i] = gbm_bo_get_handle_for_plane(m_bo, i).u32;
            strides[i] = gbm_bo_get_stride_for_plane(m_bo, i);
            offsets[i] = gbm_bo_get_offset(m_bo, i);
            modifiers[i] = gbm_bo_get_modifier(m_bo);
        }
    } else {
        handles[0] = gbm_bo_get_format(m_bo);
        strides[0] = gbm_bo_get_stride(m_bo);
        modifiers[0] = DRM_FORMAT_MOD_INVALID;
    }

    if (modifiers[0] != DRM_FORMAT_MOD_INVALID && m_gpu->addFB2ModifiersSupported()) {
        if (drmModeAddFB2WithModifiers(m_gpu->fd(), m_size.width(), m_size.height(), gbm_bo_get_format(m_bo), handles, strides, offsets, modifiers, &m_bufferId, DRM_MODE_FB_MODIFIERS)) {
            qCWarning(KWIN_DRM) << "drmModeAddFB2WithModifiers failed!" << strerror(errno);
        }
    } else {
        if (drmModeAddFB2(m_gpu->fd(), m_size.width(), m_size.height(), gbm_bo_get_format(m_bo), handles, strides, offsets, &m_bufferId, 0)) {
            // fallback
            if (drmModeAddFB(m_gpu->fd(), m_size.width(), m_size.height(), 24, 32, strides[0], handles[0], &m_bufferId) != 0) {
                qCWarning(KWIN_DRM) << "drmModeAddFB2 and drmModeAddFB both failed!" << strerror(errno);
            }
        }
    }

    gbm_bo_set_user_data(m_bo, this, nullptr);
}

}
