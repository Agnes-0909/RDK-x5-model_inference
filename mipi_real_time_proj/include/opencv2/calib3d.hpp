#pragma once

#include <opencv2/core.hpp>

namespace cv {

static constexpr int CALIB_ZERO_DISPARITY = 1024;

inline void Rodrigues(const Mat&, Mat& dst) {
  dst = Mat::eye(3, 3, CV_64F);
}

inline void stereoRectify(const Mat&, const Mat&, const Mat&, const Mat&, Size,
                          const Mat&, const Mat&, Mat& R1, Mat& R2, Mat& P1,
                          Mat& P2, Mat& Q, int = 0, double = -1,
                          Size = Size(), Rect* = nullptr, Rect* = nullptr) {
  R1 = Mat::eye(3, 3, CV_64F);
  R2 = Mat::eye(3, 3, CV_64F);
  P1 = Mat::eye(3, 4, CV_64F);
  P2 = Mat::eye(3, 4, CV_64F);
  Q = Mat::eye(4, 4, CV_64F);
}

inline Mat getOptimalNewCameraMatrix(const Mat& cameraMatrix, const Mat&, Size,
                                     double, Size, Rect* = nullptr,
                                     bool = false) {
  return cameraMatrix.clone();
}

inline void initUndistortRectifyMap(const Mat&, const Mat&, const Mat&,
                                    const Mat&, Size size, int m1type,
                                    Mat& map1, Mat& map2) {
  map1 = Mat::zeros(size, m1type);
  map2 = Mat::zeros(size, m1type);
}

namespace fisheye {

inline void stereoRectify(const Mat&, const Mat&, const Mat&, const Mat&, Size,
                          const Mat&, const Mat&, Mat& R1, Mat& R2, Mat& P1,
                          Mat& P2, Mat& Q, int = 0, Size = Size(),
                          double = 0.0, double = 1.0) {
  R1 = Mat::eye(3, 3, CV_64F);
  R2 = Mat::eye(3, 3, CV_64F);
  P1 = Mat::eye(3, 4, CV_64F);
  P2 = Mat::eye(3, 4, CV_64F);
  Q = Mat::eye(4, 4, CV_64F);
}

inline void initUndistortRectifyMap(const Mat&, const Mat&, const Mat&,
                                    const Mat&, Size size, int m1type,
                                    Mat& map1, Mat& map2) {
  map1 = Mat::zeros(size, m1type);
  map2 = Mat::zeros(size, m1type);
}

inline void estimateNewCameraMatrixForUndistortRectify(
    const Mat& K, const Mat&, Size, const Matx33d&, Mat& P, double = 0.0,
    Size = Size(), double = 1.0) {
  P = K.clone();
}

}  // namespace fisheye
}  // namespace cv
