#include "tracker.hpp"
#include <algorithm>
#include <cmath>

namespace auto_aim {

TrackedObject::TrackedObject(int id, const cv::Rect& b) : id(id), box(b) {
    kf.init(4, 2, 0);
    kf.transitionMatrix = (cv::Mat_<float>(4, 4) << 
        1, 0, 1, 0, 
        0, 1, 0, 1, 
        0, 0, 1, 0, 
        0, 0, 0, 1);
    cv::setIdentity(kf.measurementMatrix);
    cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(constants::KF_PROCESS_NOISE));
    cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(constants::KF_MEASUREMENT_NOISE));
    cv::setIdentity(kf.errorCovPost, cv::Scalar::all(constants::KF_ERROR_COV));
    
    kf.statePost.at<float>(0) = static_cast<float>(b.x + b.width / 2.0);
    kf.statePost.at<float>(1) = static_cast<float>(b.y + b.height / 2.0);
    kf.statePost.at<float>(2) = 0;
    kf.statePost.at<float>(3) = 0;
}

void TrackedObject::predict() {
    cv::Mat p = kf.predict();
    box.x = static_cast<int>(p.at<float>(0) - box.width / 2.0f);
    box.y = static_cast<int>(p.at<float>(1) - box.height / 2.0f);
}

void TrackedObject::update(const cv::Rect& b) {
    cv::Mat meas = (cv::Mat_<float>(2, 1) << 
        static_cast<float>(b.x + b.width / 2.0), 
        static_cast<float>(b.y + b.height / 2.0));
    kf.correct(meas);
    box = b;
    lost_frames = 0;
}

Tracker::Tracker(double distance_threshold, int max_lost_frames)
    : distance_threshold_(distance_threshold), max_lost_frames_(max_lost_frames) {
    tracks_.reserve(64);
}

void Tracker::update(const std::vector<Detection>& detections) {
    for (auto& t : tracks_) t.predict();
    
    if (tracks_.empty()) {
        for (const auto& d : detections) {
            tracks_.emplace_back(next_id_++, d.box);
        }
        return;
    }
    
    if (detections.empty()) {
        for (auto& t : tracks_) t.lost_frames++;
    } else {
        std::vector<std::vector<double>> distMatrix(tracks_.size(), 
            std::vector<double>(detections.size(), 0.0));
        
        for (size_t i = 0; i < tracks_.size(); ++i) {
            cv::Point tc(tracks_[i].box.x + tracks_[i].box.width / 2, 
                         tracks_[i].box.y + tracks_[i].box.height / 2);
            for (size_t j = 0; j < detections.size(); ++j) {
                cv::Point dc(detections[j].box.x + detections[j].box.width / 2, 
                             detections[j].box.y + detections[j].box.height / 2);
                distMatrix[i][j] = std::hypot(static_cast<double>(tc.x - dc.x), 
                                              static_cast<double>(tc.y - dc.y));
            }
        }
        
        std::vector<int> assignment;
        hungarian_.Solve(distMatrix, assignment);
        
        std::vector<bool> matched_det(detections.size(), false);
        for (size_t i = 0; i < assignment.size(); ++i) {
            int j = assignment[i];
            if (j >= 0 && static_cast<size_t>(j) < detections.size() && 
                distMatrix[i][j] < distance_threshold_) {
                tracks_[i].update(detections[j].box);
                matched_det[j] = true;
            } else {
                tracks_[i].lost_frames++;
            }
        }
        
        for (size_t j = 0; j < detections.size(); ++j) {
            if (!matched_det[j]) {
                tracks_.emplace_back(next_id_++, detections[j].box);
            }
        }
    }
    
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), 
        [this](const TrackedObject& t) { return t.lost_frames > max_lost_frames_; }), 
        tracks_.end());
}

} // namespace auto_aim
