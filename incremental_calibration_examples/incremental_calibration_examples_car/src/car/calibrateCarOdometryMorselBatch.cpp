/******************************************************************************
 * Copyright (C) 2013 by Jerome Maye                                          *
 * jerome.maye@gmail.com                                                      *
 *                                                                            *
 * This program is free software; you can redistribute it and/or modify       *
 * it under the terms of the Lesser GNU General Public License as published by*
 * the Free Software Foundation; either version 3 of the License, or          *
 * (at your option) any later version.                                        *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * Lesser GNU General Public License for more details.                        *
 *                                                                            *
 * You should have received a copy of the Lesser GNU General Public License   *
 * along with this program. If not, see <http://www.gnu.org/licenses/>.       *
 ******************************************************************************/

/** \file calibrateCarOdometryMorselBatch.cpp
    \brief This file calibrates the car from a log file captured in Morsel
           in a batch manner.
  */

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

#include <Eigen/Core>

#include <sm/kinematics/rotations.hpp>
#include <sm/kinematics/RotationVector.hpp>
#include <sm/kinematics/EulerAnglesYawPitchRoll.hpp>
#include <sm/kinematics/EulerAnglesZXY.h>

#include <bsplines/BSplinePose.hpp>

#include <aslam/splines/BSplinePoseDesignVariable.hpp>

#include <aslam/backend/OptimizationProblem.hpp>
#include <aslam/backend/Optimizer2Options.hpp>
#include <aslam/backend/SparseQrLinearSystemSolver.hpp>
#include <aslam/backend/SparseQRLinearSolverOptions.h>
#include <aslam/backend/Optimizer2.hpp>
#include <aslam/backend/EuclideanPoint.hpp>
#include <aslam/backend/RotationQuaternion.hpp>
#include <aslam/backend/TransformationExpression.hpp>
#include <aslam/backend/MEstimatorPolicies.hpp>

#include <aslam/calibration/statistics/NormalDistribution.h>
#include <aslam/calibration/data-structures/VectorDesignVariable.h>
#include <aslam/calibration/algorithms/matrixOperations.h>

#include "aslam/calibration/car/ErrorTermPose.h"
#include "aslam/calibration/car/ErrorTermOdometry.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <LogFilename>" << std::endl;
    return -1;
  }

  // car parameters
  double L_RF; // rear-front axle distance
  double e_F; // half-width front axle
  double e_R; // half-width rear axle
  double r_FL; // front left wheel radius
  double r_FR; // front right wheel radius
  double r_RL; // rear left wheel radius
  double r_RR; // rear right wheel radius

  // transformation between IMU and odometry
  double t_x, t_y, t_z; // translation
  double r_x, r_y, r_z; // rotation [rad]

  // covariance matrix of the pose measurements
  aslam::calibration::ErrorTermPose::Covariance Q =
    aslam::calibration::ErrorTermPose::Covariance::Zero();
  Q(0, 0) = 1e-4;
  Q(1, 1) = 1e-4;
  Q(2, 2) = 1e-4;
  Q(3, 3) = 1e-7;
  Q(4, 4) = 1e-7;
  Q(5, 5) = 1e-7;

  // covariance matrix of the odometry measurements
  aslam::calibration::ErrorTermOdometry::Covariance R =
    aslam::calibration::ErrorTermOdometry::Covariance::Zero();
  R(0, 0) = 1e-3;
  R(1, 1) = 1e-3;
  R(2, 2) = 1e-3;
  R(3, 3) = 1e-3;
  R(4, 4) = 1e-3;

  // read parameters from log file
  std::cout << "Parsing parameters from log file..." << std::endl;
  std::ifstream logFile(argv[1]);
  char line[256];
  if (!logFile.eof())
    logFile.getline(line, 256);
  else {
    std::cerr << "Parsing error!" << std::endl;
    return 1;
  }
  if (!logFile.eof()) {
    std::cout << line << std::endl;
    logFile.getline(line, 256);
  }
  else {
    std::cerr << "Parsing error!" << std::endl;
    return 1;
  }
  if (!logFile.eof()) {
    std::cout << line << std::endl;
    logFile.getline(line, 256);
  }
  else {
    std::cerr << "Parsing error!" << std::endl;
    return 1;
  }
  if (!logFile.eof()) {
    std::cout << line << std::endl;
    std::string lineStr(line);
    // find L_RF
    size_t pos = lineStr.find(std::string("L = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 3));
      ss >> L_RF;
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find e_F
    pos = lineStr.find(std::string("e_F = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 5));
      ss >> e_F;
      e_F /= 2;
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find e_R
    pos = lineStr.find(std::string("e_R = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 5));
      ss >> e_R;
      e_R /= 2;
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find r_FL
    pos = lineStr.find(std::string("r_FL = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 6));
      ss >> r_FL;
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find r_FR
    pos = lineStr.find(std::string("r_FR = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 6));
      ss >> r_FR;
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find r_RL
    pos = lineStr.find(std::string("r_RL = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 6));
      ss >> r_RL;
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find r_RR
    pos = lineStr.find(std::string("r_RR = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 6));
      ss >> r_RR;
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    logFile.getline(line, 256);
  }
  else {
    std::cerr << "Parsing error!" << std::endl;
    return 1;
  }
  if (!logFile.eof()) {
    std::cout << line << std::endl;
    std::string lineStr(line);
    // find t_x
    size_t pos = lineStr.find(std::string("t_x = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 5));
      ss >> t_x;
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find t_y
    pos = lineStr.find(std::string("t_y = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 5));
      ss >> t_y;
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find t_z
    pos = lineStr.find(std::string("t_z = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 5));
      ss >> t_z;
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find r_x
    pos = lineStr.find(std::string("r_x = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 5));
      ss >> r_x;
      r_x = sm::kinematics::angleMod(sm::kinematics::deg2rad(r_x));
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find r_y
    pos = lineStr.find(std::string("r_y = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 5));
      ss >> r_y;
      r_y = sm::kinematics::angleMod(sm::kinematics::deg2rad(r_y));
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    // find r_z
    pos = lineStr.find(std::string("r_z = "));
    if (pos != std::string::npos) {
      std::stringstream ss(lineStr.substr(pos + 5));
      ss >> r_z;
      r_z = sm::kinematics::angleMod(sm::kinematics::deg2rad(r_z));
    }
    else {
      std::cerr << "Parsing error!" << std::endl;
      return 1;
    }
    logFile.getline(line, 256);
  }
  else {
    std::cerr << "Parsing error!" << std::endl;
    return 1;
  }
  if (!logFile.eof()) {
    std::cout << line << std::endl;
  }
  else {
    std::cerr << "Parsing error!" << std::endl;
    return 1;
  }

  // create specific rotation parameterization for B-spline
  boost::shared_ptr<sm::kinematics::RotationVector>
    rv(new sm::kinematics::RotationVector);

  // read data from log file
  std::cout << "Parsing data from log file..." << std::endl;
  std::vector<double> timestampsParse;
  std::vector<Eigen::Matrix<double, 6, 1> > posesParse;
  std::vector<Eigen::Matrix<double, 5, 1> > odometryParse;
  std::vector<Eigen::Matrix<double, 6, 1> > velocityParse;
  const sm::kinematics::EulerAnglesYawPitchRoll ypr;
  const sm::kinematics::EulerAnglesZXY zxy;
  while (!logFile.eof()) {
    // timestamp
    double timestamp;
    logFile >> timestamp;
    timestampsParse.push_back(timestamp);

    // read raw pose, convert rotation to rad
    Eigen::Matrix<double, 6, 1> pose;
    logFile >> pose(0, 0) >> pose(1, 0) >> pose(2, 0)
      >> pose(3, 0) >> pose(4, 0) >> pose(5, 0);
    pose(3, 0) = sm::kinematics::angleMod(sm::kinematics::deg2rad(pose(3, 0)));
    pose(4, 0) = sm::kinematics::angleMod(sm::kinematics::deg2rad(pose(4, 0)));
    pose(5, 0) = sm::kinematics::angleMod(sm::kinematics::deg2rad(pose(5, 0)));

    // change roll-pitch-yaw to yaw-pitch-roll
    pose.tail<3>().reverseInPlace();

    // invert roll and pitch for the zxy convention
    const double temp = pose(5, 0);
    pose(5, 0) = pose(4, 0);
    pose(4, 0) = temp,

    // add some noise to the pose to make it a measurement
//    pose = aslam::calibration::NormalDistribution<6>(pose, Q).getSample();

    // parameterization of rotation as a rotation vector
    pose.tail<3>() = rv->rotationMatrixToParameters(
      zxy.parametersToRotationMatrix(pose.tail<3>()));

    // ensure rotation vector do not flip
    if (!posesParse.empty()) {
      // previous rotation vector
      const Eigen::Matrix<double, 3, 1> pr =
        posesParse[posesParse.size() - 1].tail<3>();
      // current rotation vector
      const Eigen::Matrix<double, 3, 1> r = pose.tail<3>();
      // current angle
      const double angle = r.norm();
      // current axis
      const Eigen::Matrix<double, 3, 1> axis = r / angle;
      // best rotation vector
      Eigen::Matrix<double, 3, 1> best_r = r;
      // best distance
      double best_dist = (best_r - pr).norm();
      // find best vector
      for (int s = -3; s <= 4; ++s) {
        const Eigen::Matrix<double, 3, 1> aa = axis * (angle + M_PI * 2.0 * s);
        const double dist = (aa - pr).norm();
        if (dist < best_dist) {
          best_r = aa;
          best_dist = dist;
        }
      }
      pose.tail<3>() = best_r;
    }

    // store it
    posesParse.push_back(pose);

    // translational and angular velocity
    Eigen::Matrix<double, 6, 1> velocity;
    logFile >> velocity(0, 0) >> velocity(1, 0) >> velocity(2, 0)
      >> velocity(3, 0) >> velocity(4, 0) >> velocity(5, 0);
    velocity(3, 0) = sm::kinematics::deg2rad(velocity(3, 0));
    velocity(4, 0) = sm::kinematics::deg2rad(velocity(4, 0));
    velocity(5, 0) = sm::kinematics::deg2rad(velocity(5, 0));
    velocityParse.push_back(velocity);

    // odometry measurements
    Eigen::Matrix<double, 5, 1> odometryFileRead;
    logFile >> odometryFileRead(0, 0) >> odometryFileRead(1, 0)
      >> odometryFileRead(2, 0) >> odometryFileRead(3, 0)
      >> odometryFileRead(4, 0);

    // convert odometry to what we expect
    Eigen::Matrix<double, 5, 1> odometry;
    odometry(0, 0) = sm::kinematics::deg2rad(odometryFileRead(4, 0));
    odometry(1, 0) = sm::kinematics::deg2rad(odometryFileRead(2, 0));
    odometry(2, 0) = sm::kinematics::deg2rad(odometryFileRead(3, 0));
    odometry(3, 0) = sm::kinematics::deg2rad(odometryFileRead(0, 0));
    odometry(4, 0) = sm::kinematics::deg2rad(odometryFileRead(1, 0));

    // add some noise to the odometry to make it a measurement
    odometry =
      aslam::calibration::NormalDistribution<5>(odometry, R).getSample();

    // store odometry
    odometryParse.push_back(odometry);
  }
  // kick out the last stuff, problem of parsing?
  timestampsParse.pop_back();
  posesParse.pop_back();
  odometryParse.pop_back();
  velocityParse.pop_back();

  // fill in data for handling with B-spline
  Eigen::VectorXd timestamps(timestampsParse.size());
  Eigen::Matrix<double, 6, Eigen::Dynamic> poses(6, timestampsParse.size());
  for (size_t i = 0; i < timestampsParse.size(); ++i) {
    timestamps(i) = timestampsParse[i];
    poses.col(i) = posesParse[i];
  }

  std::cout << "Number of measurements: " << timestampsParse.size()
    << std::endl;
  const double elapsedTime =
    timestampsParse[timestampsParse.size() - 1] - timestampsParse[0];
  std::cout << "Sequence length [s]: " << elapsedTime << std::endl;

  // fill the B-spline with measurements
  const double lambda = 1e-6;
  const int measPerSec = timestampsParse.size() / elapsedTime;
  const int measPerSecDesired = 5;
  int numSegments;
  if (measPerSec > measPerSecDesired)
    numSegments = measPerSecDesired * elapsedTime;
  else
    numSegments = timestampsParse.size();
  std::cout << "Creating B-spline with " << numSegments << " segments..."
    << std::endl;
  const int order = 4;
  bsplines::BSplinePose bspline(order, rv);
  bspline.initPoseSplineSparse(timestamps, poses, numSegments, lambda);

  // output poses from spline
  std::cout << "Outputting to file..." << std::endl;
  std::ofstream outFile("bsplinePoses.txt");
  for (size_t i = 0; i < timestampsParse.size(); ++i) {
    outFile << std::fixed << std::setprecision(16)
      << timestampsParse[i]
      << " " << bspline.position(timestampsParse[i]).transpose()
      << " " << zxy.rotationMatrixToParameters(
        bspline.orientation(timestampsParse[i])).transpose()
      << " " << bspline.linearVelocityBodyFrame(timestampsParse[i]).transpose()
      << " " << bspline.angularVelocityBodyFrame(timestampsParse[i]).transpose()
      << " " << bspline.linearVelocity(timestampsParse[i]).transpose()
      << " " << bspline.angularVelocity(timestampsParse[i]).transpose()
      << std::endl;
  }

  // create optimization problem
  boost::shared_ptr<aslam::backend::OptimizationProblem> problem(
    new aslam::backend::OptimizationProblem);

  // create B-spline design variable
  std::cout << "Creating B-spline design variable..." << std::endl;
  boost::shared_ptr<aslam::splines::BSplinePoseDesignVariable>
    bspdv(new aslam::splines::BSplinePoseDesignVariable(bspline));
  for (size_t i = 0; i < bspdv->numDesignVariables(); ++i) {
//    bspdv->designVariable(i)->setActive(true);
    problem->addDesignVariable(bspdv->designVariable(i), false);
  }

  // create error terms for each pose measurement
  std::cout << "Creating error terms for pose measurements..." << std::endl;
  for (size_t i = 0; i < posesParse.size(); ++i) {
    aslam::calibration::ErrorTermPose::Input xm;
    xm.head<3>() = posesParse[i].head<3>();
    xm.tail<3>() = ypr.rotationMatrixToParameters(
      rv->parametersToRotationMatrix(posesParse[i].tail<3>()));
    boost::shared_ptr<aslam::calibration::ErrorTermPose> e_pose(
      new aslam::calibration::ErrorTermPose(
      bspdv->transformation(timestampsParse[i]), xm, Q));
//    problem->addErrorTerm(e_pose);
  }

  // create car parameters design variable
  std::cout << "Creating car parameters design variable..." << std::endl;
  const double L =
    aslam::calibration::NormalDistribution<1>(L_RF, 1e-12).getSample();
  const double e_r = aslam::calibration::NormalDistribution<1>(
    e_R, 1e-12).getSample();
  const double e_f = aslam::calibration::NormalDistribution<1>(
    e_F, 1e-12).getSample();
  const double a0 =
    aslam::calibration::NormalDistribution<1>(0, 1e-12).getSample();
  const double a1 =
    aslam::calibration::NormalDistribution<1>(1.0, 1e-12).getSample();
  const double a2 =
    aslam::calibration::NormalDistribution<1>(0, 1e-12).getSample();
  const double a3 =
    aslam::calibration::NormalDistribution<1>(0, 1e-12).getSample();
  const double k_rl =
    aslam::calibration::NormalDistribution<1>(r_RL, 1e-12).getSample();
  const double k_rr =
    aslam::calibration::NormalDistribution<1>(r_RR, 1e-12).getSample();
  const double k_fl =
    aslam::calibration::NormalDistribution<1>(r_FL, 1e-12).getSample();
  const double k_fr =
    aslam::calibration::NormalDistribution<1>(r_FR, 1e-12).getSample();
  boost::shared_ptr<aslam::calibration::VectorDesignVariable<11> > cpdv(
    new aslam::calibration::VectorDesignVariable<11>(
    (aslam::calibration::VectorDesignVariable<11>::Container() <<
    L, e_r, e_f, a0, a1, a2, a3, k_rl, k_rr, k_fl, k_fr).finished()));
  cpdv->setActive(true);
  problem->addDesignVariable(cpdv);

  // transformation between IMU and odometry design variable
  Eigen::Matrix<double, 3, 1> t_odo(t_x, t_y, t_z);
  Eigen::Matrix<double, 3, 1> r_odo(r_z, r_x, r_y);
  Eigen::Matrix<double, 3, 3> S = Eigen::Matrix<double, 3, 3>::Zero();
  S(0, 0) = 1e-4; S(1, 1) = 1e-4; S(2, 2) = 1e-4;
//  t_odo = aslam::calibration::NormalDistribution<3>(t_odo, S).getSample();
//  r_odo = aslam::calibration::NormalDistribution<3>(r_odo, S).getSample();
  boost::shared_ptr<aslam::backend::EuclideanPoint> t_io_dv(
    new aslam::backend::EuclideanPoint(t_odo));
//  t_io_dv->setActive(true);
  boost::shared_ptr<aslam::backend::RotationQuaternion> C_io_dv(
    new aslam::backend::RotationQuaternion(
    zxy.parametersToRotationMatrix(r_odo)));
//  C_io_dv->setActive(true);
  aslam::backend::RotationExpression C_io(C_io_dv);
  aslam::backend::EuclideanExpression t_io(t_io_dv);
  problem->addDesignVariable(t_io_dv);
  problem->addDesignVariable(C_io_dv);

  std::ofstream errorFile("errors.txt");

  // create error terms for each odometry measurement
  std::cout << "Creating error terms for odometry measurements..." << std::endl;
  for (size_t i = 0; i < odometryParse.size(); ++i) {
    // translational velocity of IMU center in its own reference frame
    boost::shared_ptr<aslam::backend::EuclideanPoint> v_ii_c_dv(
      new aslam::backend::EuclideanPoint(velocityParse[i].head<3>()));
    aslam::backend::EuclideanExpression v_ii_c(v_ii_c_dv);

    // angular velocity of IMU center in its own reference frame
    boost::shared_ptr<aslam::backend::EuclideanPoint> om_ii_c_dv(
      new aslam::backend::EuclideanPoint(velocityParse[i].tail<3>()));
    aslam::backend::EuclideanExpression om_ii_c(om_ii_c_dv);

    // translational velocity of odometry center in its own reference frame
    aslam::backend::EuclideanExpression v_oo = C_io.inverse() *
      (v_ii_c + om_ii_c.cross(t_io));

    // angular velocity of odometry center in its own reference frame
    aslam::backend::EuclideanExpression om_oo = C_io.inverse() * om_ii_c;

    // skip when velocity is null
    if (fabs(v_oo.toValue()(0)) < std::numeric_limits<double>::epsilon() &&
        fabs(om_oo.toValue()(2)) < std::numeric_limits<double>::epsilon())
      continue;

    // create and add the error term
    boost::shared_ptr<aslam::calibration::ErrorTermOdometry> e_odo(
      new aslam::calibration::ErrorTermOdometry(v_oo, om_oo, cpdv.get(),
      odometryParse[i], R));

    problem->addErrorTerm(e_odo);
    errorFile << e_odo->evaluateError() << std::endl;
    e_odo->setMEstimatorPolicy(
      boost::shared_ptr<aslam::backend::BlakeZissermanMEstimator>(
      new aslam::backend::BlakeZissermanMEstimator(e_odo->dimension(),
      0.999, 0.1)));
  }

  // optimize
  std::cout << "Initial guess: " << std::endl;
  std::cout << "Odometry parameters: " << std::endl;
  std::cout << *cpdv << std::endl;
  std::cout << "Translation IMU-ODO: " << std::endl;
  std::cout << t_io.toValue().transpose() << std::endl;
  std::cout << "Rotation IMU-ODO: " << std::endl;
  std::cout <<
    zxy.rotationMatrixToParameters(C_io.toRotationMatrix()).transpose()
    << std::endl;
  std::cout << "Optimizing..." << std::endl;
  aslam::backend::Optimizer2Options options;
  options.verbose = true;
  options.doLevenbergMarquardt = false;
  options.linearSolver = "sparse_qr";
  aslam::backend::SparseQRLinearSolverOptions linearSolverOptions;
  linearSolverOptions.colNorm = true;
//  linearSolverOptions.qrTol = -1;
  aslam::backend::Optimizer2 optimizer(options);
  optimizer.getSolver<aslam::backend::SparseQrLinearSystemSolver>()
    ->setOptions(linearSolverOptions);
  optimizer.setProblem(problem);
  optimizer.optimize();
  std::cout << "Estimated parameters: " << std::endl;
  std::cout << "Odometry parameters: " << std::endl;
  std::cout << *cpdv << std::endl;
  std::cout << "Translation IMU-ODO: " << std::endl;
  std::cout << t_io.toValue().transpose() << std::endl;
  std::cout << "Rotation IMU-ODO: " << std::endl;
  std::cout <<
    zxy.rotationMatrixToParameters(C_io.toRotationMatrix()).transpose()
    << std::endl;
  std::cout << "True values: " << std::endl;
  std::cout << "Odometry parameters: " << std::endl;
  std::cout << L_RF << " " << e_R << " " << e_F << " " << 0 << " "
    << 1 << " " << 0 << " " << 0 << " " << r_RL << " " << r_RR << " "
    << r_FL << " " << r_FR << std::endl;
  std::cout << "Translation IMU-ODO: " << std::endl;
  std::cout << t_x << " " << t_y << " " << t_z << std::endl;
  std::cout << "Rotation IMU-ODO: " << std::endl;
  std::cout << r_z << " " << r_x << " " << r_y << std::endl;
  const aslam::backend::CompressedColumnMatrix<ssize_t>& RFactor =
    optimizer.getSolver<aslam::backend::SparseQrLinearSystemSolver>()->getR();
  const size_t numCols = RFactor.cols();
  Eigen::MatrixXd Sigma =
    aslam::calibration::computeCovariance(RFactor, 0, numCols - 1);
  std::cout << "Sigma: " << std::endl;
  std::cout << std::fixed << std::setprecision(16) <<
    Sigma.diagonal().transpose() << std::endl;
  std::cout << "SumLogDiagR: "
    << aslam::calibration::computeSumLogDiagR(RFactor, 0, numCols - 1)
    << std::endl;

  // output poses from spline
  std::cout << "Outputting to file..." << std::endl;
  std::ofstream outFile2("bsplinePosesOptimized.txt");
  for (size_t i = 0; i < timestampsParse.size(); ++i) {
    const Eigen::Vector3d r =
      bspdv->position(timestampsParse[i]).toEuclidean();
    const Eigen::Matrix3d C =
      bspdv->orientation(timestampsParse[i]).toRotationMatrix();
    const Eigen::Vector3d v =
      bspdv->linearVelocity(timestampsParse[i]).toEuclidean();
    const Eigen::Vector3d om =
      bspdv->angularVelocityBodyFrame(timestampsParse[i]).toEuclidean();
    outFile2 << std::fixed << std::setprecision(16)
      << timestampsParse[i]
      << " " << r.transpose()
      << " " << zxy.rotationMatrixToParameters(C).transpose()
      << " " << v.transpose()
      << " " << om.transpose()
      << " " << v.transpose()
      << " " << om.transpose()
      << std::endl;
  }

  return 0;
}