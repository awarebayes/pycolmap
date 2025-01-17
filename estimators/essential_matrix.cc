// Authors: Mihai-Dusmanu (mihaidusmanu), Paul-Edouard Sarlin (skydes)

#include "colmap/geometry/essential_matrix.h"

#include "colmap/estimators/essential_matrix.h"
#include "colmap/geometry/pose.h"
#include "colmap/math/random.h"
#include "colmap/optim/loransac.h"
#include "colmap/scene/camera.h"

#include <fstream>
#include <iostream>

using namespace colmap;

#include <pybind11/eigen.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace pybind11::literals;

#include "log_exceptions.h"

py::dict essential_matrix_estimation(
    const std::vector<Eigen::Vector2d> points2D1,
    const std::vector<Eigen::Vector2d> points2D2,
    Camera& camera1,
    Camera& camera2,
    const RANSACOptions options) {
  SetPRNGSeed(0);

  // Check that both vectors have the same size.
  THROW_CHECK_EQ(points2D1.size(), points2D2.size());

  // Failure output dictionary.
  py::dict failure_dict("success"_a = false);
  py::gil_scoped_release release;

  // Image to world.
  std::vector<Eigen::Vector2d> world_points2D1;
  for (size_t idx = 0; idx < points2D1.size(); ++idx) {
    world_points2D1.push_back(camera1.CamFromImg(points2D1[idx]));
  }

  std::vector<Eigen::Vector2d> world_points2D2;
  for (size_t idx = 0; idx < points2D2.size(); ++idx) {
    world_points2D2.push_back(camera2.CamFromImg(points2D2[idx]));
  }

  // Compute world error.
  const double max_error_px = options.max_error;
  const double max_error = 0.5 * (max_error_px / camera1.MeanFocalLength() +
                                  max_error_px / camera2.MeanFocalLength());
  RANSACOptions ransac_options(options);
  ransac_options.max_error = max_error;

  LORANSAC<EssentialMatrixFivePointEstimator, EssentialMatrixFivePointEstimator>
      ransac(ransac_options);

  // Essential matrix estimation.
  const auto report = ransac.Estimate(world_points2D1, world_points2D2);

  if (!report.success) {
    return failure_dict;
  }

  // Recover data from report.
  const Eigen::Matrix3d E = report.model;
  const size_t num_inliers = report.support.num_inliers;
  const auto inlier_mask = report.inlier_mask;

  // Pose from essential matrix.
  std::vector<Eigen::Vector2d> inlier_world_points2D1;
  std::vector<Eigen::Vector2d> inlier_world_points2D2;

  for (size_t idx = 0; idx < inlier_mask.size(); ++idx) {
    if (inlier_mask[idx]) {
      inlier_world_points2D1.push_back(world_points2D1[idx]);
      inlier_world_points2D2.push_back(world_points2D2[idx]);
    }
  }

  Rigid3d cam2_from_cam1;
  Eigen::Matrix3d cam2_from_cam1_rot_mat;
  std::vector<Eigen::Vector3d> points3D;
  PoseFromEssentialMatrix(E,
                          inlier_world_points2D1,
                          inlier_world_points2D2,
                          &cam2_from_cam1_rot_mat,
                          &cam2_from_cam1.translation,
                          &points3D);
  cam2_from_cam1.rotation = Eigen::Quaterniond(cam2_from_cam1_rot_mat);

  // Convert vector<char> to vector<int>.
  std::vector<bool> inliers;
  for (auto it : inlier_mask) {
    if (it) {
      inliers.push_back(true);
    } else {
      inliers.push_back(false);
    }
  }

  // Success output dictionary.
  py::gil_scoped_acquire acquire;
  py::dict success_dict("success"_a = true,
                        "E"_a = E,
                        "cam2_from_cam1"_a = cam2_from_cam1,
                        "num_inliers"_a = num_inliers,
                        "inliers"_a = inliers);
  return success_dict;
}

py::dict essential_matrix_estimation(
    const std::vector<Eigen::Vector2d> points2D1,
    const std::vector<Eigen::Vector2d> points2D2,
    Camera& camera1,
    Camera& camera2,
    const double max_error_px,
    const double min_inlier_ratio,
    const int min_num_trials,
    const int max_num_trials,
    const double confidence) {
  RANSACOptions ransac_options;
  ransac_options.max_error = max_error_px;
  ransac_options.min_inlier_ratio = min_inlier_ratio;
  ransac_options.min_num_trials = min_num_trials;
  ransac_options.max_num_trials = max_num_trials;
  ransac_options.confidence = confidence;
  return essential_matrix_estimation(
      points2D1, points2D2, camera1, camera2, ransac_options);
}

void bind_essential_matrix_estimation(py::module& m) {
  auto est_options = m.attr("RANSACOptions")().cast<RANSACOptions>();

  m.def("essential_matrix_estimation",
        static_cast<py::dict (*)(const std::vector<Eigen::Vector2d>,
                                 const std::vector<Eigen::Vector2d>,
                                 Camera&,
                                 Camera&,
                                 const RANSACOptions)>(
            &essential_matrix_estimation),
        "points2D1"_a,
        "points2D2"_a,
        "camera1"_a,
        "camera2"_a,
        "estimation_options"_a = est_options,
        "LORANSAC + 5-point algorithm.");

  m.def("essential_matrix_estimation",
        static_cast<py::dict (*)(const std::vector<Eigen::Vector2d>,
                                 const std::vector<Eigen::Vector2d>,
                                 Camera&,
                                 Camera&,
                                 const double,
                                 const double,
                                 const int,
                                 const int,
                                 const double)>(&essential_matrix_estimation),
        "points2D1"_a,
        "points2D2"_a,
        "camera1"_a,
        "camera2"_a,
        "max_error_px"_a = est_options.max_error,
        "min_inlier_ratio"_a = est_options.min_inlier_ratio,
        "min_num_trials"_a = est_options.min_num_trials,
        "max_num_trials"_a = est_options.max_num_trials,
        "confidence"_a = est_options.confidence,
        "LORANSAC + 5-point algorithm.");
}
