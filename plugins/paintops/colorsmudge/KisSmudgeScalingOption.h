/*
 *  SPDX-FileCopyrightText: 2022 Jan Boon <jan.boon@kaetemi.be>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_SMUDGE_SCALING_OPTION_H
#define KIS_SMUDGE_SCALING_OPTION_H

#include <kis_curve_option.h>

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
