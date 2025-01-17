// Authors: Mihai-Dusmanu (mihaidusmanu), Paul-Edouard Sarlin (skydes)

#include "colmap/estimators/fundamental_matrix.h"

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

py::dict fundamental_matrix_estimation(
    const std::vector<Eigen::Vector2d> points2D1,
    const std::vector<Eigen::Vector2d> points2D2,
    const RANSACOptions options) {
  SetPRNGSeed(0);

  // Check that both vectors have the same size.
  THROW_CHECK_EQ(points2D1.size(), points2D2.size());

  // Failure output dictionary.
  py::dict failure_dict;
  failure_dict["success"] = false;
  py::gil_scoped_release release;

  LORANSAC<FundamentalMatrixSevenPointEstimator,
           FundamentalMatrixEightPointEstimator>
      ransac(options);

  // Fundamental matrix estimation.
  const auto report = ransac.Estimate(points2D1, points2D2);

  if (!report.success) {
    return failure_dict;
  }

  // Recover data from report.
  const Eigen::Matrix3d F = report.model;
  const size_t num_inliers = report.support.num_inliers;
  const auto inlier_mask = report.inlier_mask;

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
  py::dict success_dict;
  success_dict["success"] = true;
  success_dict["F"] = F;
  success_dict["num_inliers"] = num_inliers;
  success_dict["inliers"] = inliers;

  return success_dict;
}

py::dict fundamental_matrix_estimation(
    const std::vector<Eigen::Vector2d> points2D1,
    const std::vector<Eigen::Vector2d> points2D2,
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
  return fundamental_matrix_estimation(points2D1, points2D2, ransac_options);
}

void bind_fundamental_matrix_estimation(py::module& m) {
  auto est_options = m.attr("RANSACOptions")().cast<RANSACOptions>();

  m.def("fundamental_matrix_estimation",
        static_cast<py::dict (*)(const std::vector<Eigen::Vector2d>,
                                 const std::vector<Eigen::Vector2d>,
                                 const RANSACOptions)>(
            &fundamental_matrix_estimation),
        "points2D1"_a,
        "points2D2"_a,
        "estimation_options"_a = est_options,
        "LORANSAC + 7-point algorithm.");

  m.def("fundamental_matrix_estimation",
        static_cast<py::dict (*)(const std::vector<Eigen::Vector2d>,
                                 const std::vector<Eigen::Vector2d>,
                                 const double,
                                 const double,
                                 const int,
                                 const int,
                                 const double)>(&fundamental_matrix_estimation),
        "points2D1"_a,
        "points2D2"_a,
        "max_error_px"_a = est_options.max_error,
        "min_inlier_ratio"_a = est_options.min_inlier_ratio,
        "min_num_trials"_a = est_options.min_num_trials,
        "max_num_trials"_a = est_options.max_num_trials,
        "confidence"_a = est_options.confidence,
        "LORANSAC + 7-point algorithm.");
}
