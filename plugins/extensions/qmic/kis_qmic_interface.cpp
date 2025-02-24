/*
 * This file is part of Krita
 *
 * SPDX-FileCopyrightText: 2020 L. E. Segovia <amy@amyspark.me>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_qmic_interface.h"

#include <KisImageSignals.h>
#include <KisViewManager.h>
#include <kis_algebra_2d.h>
#include <kis_debug.h>
#include <kis_image.h>
#include <kis_image_barrier_locker.h>
#include <kis_input_output_mapper.h>
#include <kis_processing_applicator.h>
#include <kis_selection.h>
#include <kundo2magicstring.h>

#include "gmic.h"
#include "kis_qmic_processing_visitor.h"
#include "kis_qmic_simple_convertor.h"
#include "kis_qmic_synchronize_image_size_command.h"
#include "kis_qmic_synchronize_layers_command.h"

struct KisImageInterface::Private {
    Private() = default;

    KisViewManager *m_viewManager{nullptr};
    InputLayerMode m_inputMode{InputLayerMode::Active};
    OutputMode m_outputMode{OutputMode::InPlace};
    QVector<KisQMicImageSP> m_sharedMemorySegments{};
    KisQmicApplicator *m_gmicApplicator{nullptr};
};

KisImageInterface::KisImageInterface(KisViewManager *parent)
    : p(new Private)
{
    p->m_viewManager = parent;
    KIS_ASSERT(p->m_viewManager);
}

KisImageInterface::~KisImageInterface() = default;

QSize KisImageInterface::gmic_qt_get_image_size(int mode)
{
    KisSelectionSP selection = p->m_viewManager->image()->globalSelection();

    if (selection) {
        QRect selectionRect = selection->selectedExactRect();
        return selectionRect.size();
    } else {
        p->m_inputMode = static_cast<InputLayerMode>(mode);

        QSize size(0, 0);

        dbgPlugins << "getImageSize()" << mode;

        KisInputOutputMapper mapper(p->m_viewManager->image(),
                                    p->m_viewManager->activeNode());
        KisNodeListSP nodes = mapper.inputNodes(p->m_inputMode);
        if (nodes->isEmpty()) {
            return size;
        }

        switch (p->m_inputMode) {
        case InputLayerMode::NoInput:
        case InputLayerMode::AllInvisible:
            break;
        case InputLayerMode::Active:
        case InputLayerMode::ActiveAndBelow: {
            // The last layer in the list is always the layer the user
            // has selected in Paint.NET, so it will be treated as the
            // active layer. The clipboard layer (if present) will be
            // placed above the active layer.
            const auto activeLayer = nodes->last();

            if (activeLayer && activeLayer->paintDevice()) {
                KisSelectionSP selection =
                    p->m_viewManager->image()->globalSelection();

                QRect cropRect = [&]() {
                    if (selection) {
                        return selection->selectedExactRect();
                    } else {
                        return activeLayer->exactBounds();
                    }
                }();

                size = cropRect.size();
            }
        } break;
        case InputLayerMode::All:
        case InputLayerMode::ActiveAndAbove:
        case InputLayerMode::AllVisible:
            for (auto &node : *nodes) {
                if (node && node->paintDevice()) {
                    KisSelectionSP selection =
                        p->m_viewManager->image()->globalSelection();

                    QRect cropRect = [&]() {
                        if (selection) {
                            return selection->selectedExactRect();
                        } else {
                            return node->exactBounds();
                        }
                    }();

                    size.setWidth(std::max(size.width(), cropRect.width()));
                    size.setHeight(std::max(size.height(), cropRect.height()));
                }
            }
            break;
        case InputLayerMode::AllVisiblesDesc_DEPRECATED:
        case InputLayerMode::AllInvisiblesDesc_DEPRECATED:
        case InputLayerMode::AllDesc_DEPRECATED: {
            warnPlugins << "Inputmode" << static_cast<int>(p->m_inputMode)
                        << "is not supported by GMic anymore";
            break;
        }
        default: {
            warnPlugins
                << "Inputmode" << static_cast<int>(p->m_inputMode)
                << "must be specified by GMic or is not implemented in Krita";
            break;
        }
        }

        return size;
    }
}

QVector<KisQMicImageSP> KisImageInterface::gmic_qt_get_cropped_images(int inputMode, QRectF &rc)
{
    // Create the shared memory segments, and create a new "message" to send back

    QVector<KisQMicImageSP> message;

    if (!p->m_viewManager)
        return {};

    KisImageBarrierLocker locker(p->m_viewManager->image());

    p->m_inputMode = static_cast<InputLayerMode>(inputMode);

    dbgPlugins << "prepareCroppedImages()" << message << rc << inputMode;

    KisInputOutputMapper mapper(p->m_viewManager->image(), p->m_viewManager->activeNode());
    KisNodeListSP nodes = mapper.inputNodes(p->m_inputMode);
    if (nodes->isEmpty()) {
        return {};
    }

    for (auto &node : *nodes) {
         if (node && node->paintDevice()) {
            KisSelectionSP selection = p->m_viewManager->image()->globalSelection();

            QRect cropRect = [&]() {
                if (selection) {
                    return selection->selectedExactRect();
                } else {
                    return node->exactBounds();
                }
            }();

            dbgPlugins << "Converting node" << node->name() << cropRect;

            const QRectF mappedRect = KisAlgebra2D::mapToRect(cropRect).mapRect(rc);
            const QRect resultRect = mappedRect.toAlignedRect();

            QString noParenthesisName(node->name());
            noParenthesisName.replace(QChar('('), QChar(21)).replace(QChar(')'), QChar(22));

            const auto translatedMode = KisQmicSimpleConvertor::blendingModeToString(node->compositeOpId());

            const QString name = QString("mode(%1),opacity(%2),pos(%3,%4),name(%5)")
                               .arg(translatedMode)
                               .arg(node->percentOpacity())
                               .arg(cropRect.x())
                               .arg(cropRect.y())
                               .arg(noParenthesisName);

            auto m = KisQMicImageSP::create(name, resultRect.width(), resultRect.height(), 4);
            p->m_sharedMemorySegments << m;

            {
                QMutexLocker lock(&m->m_mutex);

                gmic_image<float> img;
                img.assign(static_cast<unsigned int>(resultRect.width()),
                           static_cast<unsigned int>(resultRect.height()),
                           1,
                           4);

                img._data = m->m_data;
                img._is_shared = true;

                KisQmicSimpleConvertor::convertToGmicImageFast(
                    node->paintDevice(),
                    img,
                    resultRect);
            }

            message << m;
        }
    }

    dbgPlugins << message;

    return message;
}

void KisImageInterface::gmic_qt_detach()
{
    for (auto memorySegment : p->m_sharedMemorySegments) {
        dbgPlugins << "detaching" << memorySegment;
        memorySegment.clear();
    }
    p->m_sharedMemorySegments.clear();
}

void KisImageInterface::gmic_qt_output_images(int mode, QVector<KisQMicImageSP> layers)
{
    // Parse the message. read the shared memory segments, fix up the current image and send an ack
    dbgPlugins << "gmic_qt_output_images";
    p->m_outputMode = (OutputMode)mode;
    if (p->m_outputMode != OutputMode::InPlace) {
        errPlugins << "Requested mode" << static_cast<int>(p->m_outputMode) << "which is not implemented yet";
        p->m_outputMode = OutputMode::InPlace;
    }

    dbgPlugins << "slotStartApplicator();" << layers;
    if (!p->m_viewManager)
        return;
    // Create a vector of gmic images

    QVector<gmic_image<float> *> images;

    for (const auto &image : layers) {
        QString layerName = image->m_layerName;
        const auto spectrum = image->m_spectrum;
        const auto width = image->m_width;
        const auto height = image->m_height;

        dbgPlugins << "Received image: " << static_cast<void *>(image.data())
                   << layerName << width << height;

        gmic_image<float> *gimg = nullptr;

        {
            QMutexLocker lock(&image->m_mutex);

            dbgPlugins << "Memory segment" << static_cast<void *>(image.data())
                       << image->size()
                       << static_cast<const void *>(image->constData())
                       << static_cast<void *>(image->m_data);
            gimg = new gmic_image<float>();
            gimg->assign(static_cast<unsigned int>(width),
                         static_cast<unsigned int>(height),
                         1,
                         static_cast<unsigned int>(spectrum));
            gimg->name = layerName;

            gimg->_data =
                new float[static_cast<size_t>(width * height * spectrum)
                          * sizeof(float)];
            dbgPlugins << "width" << width << "height" << height << "size"
                       << static_cast<size_t>(width * height * spectrum)
                    * sizeof(float)
                       << "shared memory size" << image->size();
            memcpy(gimg->_data,
                   image->constData(),
                   static_cast<size_t>(width * height * spectrum)
                       * sizeof(float));

            dbgPlugins << "created gmic image" << gimg->name << gimg->_width << gimg->_height;
        }
        images.append(gimg);
    }

    dbgPlugins << "Got" << images.size() << "gmic images";

    {
        // Start the applicator
        KUndo2MagicString actionName = kundo2_i18n("G'MIC filter");
        KisNodeSP rootNode = p->m_viewManager->image()->root();
        KisInputOutputMapper mapper(p->m_viewManager->image(), p->m_viewManager->activeNode());
        KisNodeListSP mappedLayers = mapper.inputNodes(p->m_inputMode);
        // p->m_gmicApplicator->setProperties(p->m_viewManager->image(), rootNode, images, actionName, layers);
        // p->m_gmicApplicator->apply();

        KisImageSignalVector emitSignals;
        emitSignals << ComplexSizeChangedSignal();

        KisProcessingApplicator applicator(p->m_viewManager->image(),
                                           rootNode,
                                           KisProcessingApplicator::RECURSIVE | KisProcessingApplicator::NO_UI_UPDATES,
                                           emitSignals,
                                           actionName);
        dbgPlugins << "Created applicator " << &applicator;

        QRect layerSize;
        KisSelectionSP selection = p->m_viewManager->image()->globalSelection();
        if (selection) {
            layerSize = selection->selectedExactRect();
        } else {
            layerSize = QRect(0, 0, p->m_viewManager->image()->width(), p->m_viewManager->image()->height());
        }

        // This is a three-stage process.

        if (!selection) {
            // 1. synchronize Krita image size with biggest gmic layer size
            applicator.applyCommand(new KisQmicSynchronizeImageSizeCommand(images, p->m_viewManager->image()));
        }

        // 2. synchronize layer count and convert excess GMic nodes to paint layers
        applicator.applyCommand(new KisQmicSynchronizeLayersCommand(mappedLayers, images, p->m_viewManager->image(), layerSize, selection),
                                KisStrokeJobData::SEQUENTIAL,
                                KisStrokeJobData::EXCLUSIVE);

        // 3. visit the existing nodes and reuse them to apply the remaining changes from GMic
        applicator.applyVisitor(new KisQmicProcessingVisitor(mappedLayers, images, layerSize, selection),
                                KisStrokeJobData::SEQUENTIAL); // undo information is stored in this visitor

        applicator.end();
    }
}

QDebug operator<<(QDebug d, const KisQMicImage &model)
{
    d << QString("0x%1,%2,%3,%4").arg((quintptr)&model).arg(QString(model.m_layerName.toUtf8().toHex())).arg(model.m_width).arg(model.m_height);
    return d;
}
