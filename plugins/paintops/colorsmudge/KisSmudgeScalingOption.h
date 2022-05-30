/* This file is part of the KDE project
 *
 * SPDX-FileCopyrightText: 2022 Jan Boon <jan.boon@kaetemi.be>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KIS_SMUDGE_SCALING_OPTION_H
#define KIS_SMUDGE_SCALING_OPTION_H

#include <kis_curve_option.h>
#include <brushengine/kis_paint_information.h>
#include <kis_types.h>

class KisPainter;

class KisSmudgeScalingOption: public KisCurveOption
{
public:
    KisSmudgeScalingOption();

    void setSmudgeScaling(qreal rate) {
        KisCurveOption::setValue(rate);
    }

    qreal getSmudgeScaling() const {
        return KisCurveOption::value();
    }
};

#endif // KIS_SMUDGE_SCALING_OPTION_H
