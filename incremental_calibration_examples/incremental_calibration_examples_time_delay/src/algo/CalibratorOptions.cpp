/******************************************************************************
 * Copyright (C) 2013 by Jerome Maye                                          *
 * jerome.maye@gmail.com                                                      *
 ******************************************************************************/

#include "aslam/calibration/time-delay/algo/CalibratorOptions.h"

#include <sm/PropertyTree.hpp>

using namespace sm;

namespace aslam {
  namespace calibration {

/******************************************************************************/
/* Constructors and Destructor                                                */
/******************************************************************************/

    CalibratorOptions::CalibratorOptions() :
        windowDuration(10.0),
        transSplineLambda(0.0),
        rotSplineLambda(0.0),
        splineKnotsPerSecond(5),
        transSplineOrder(4),
        rotSplineOrder(4),
        lwVariance(1e-3),
        rwVariance(1e-3),
        vyVariance(1e-1),
        vzVariance(1e-1),
        verbose(true),
        delayBound(50000000) {
    }

    CalibratorOptions::CalibratorOptions(const PropertyTree& config) {
      windowDuration = config.getDouble("windowDuration");
      verbose = config.getBool("verbose");
      delayBound = config.getInt("delayBound");

      transSplineLambda = config.getDouble("splines/transSplineLambda");
      rotSplineLambda = config.getDouble("splines/rotSplineLambda");
      splineKnotsPerSecond = config.getInt("splines/splineKnotsPerSecond");
      transSplineOrder = config.getInt("splines/transSplineOrder",
        transSplineOrder);
      rotSplineOrder = config.getInt("splines/rotSplineOrder");

      lwVariance = config.getDouble("odometry/sensors/wss/noise/lwVariance");
      rwVariance = config.getDouble("odometry/sensors/wss/noise/rwVariance");
      vyVariance = config.getDouble(
        "odometry/constraints/noise/vyVariance");
      vzVariance = config.getDouble(
        "odometry/constraints/noise/vzVariance");
    }

  }
}
