#pragma once
#include <vector>
#include <opencv2/opencv.hpp>
#include "types.hpp"
#include "../utils/constants.hpp"
#include "../third_party/hungarian.hpp"

namespace auto_aim {

struct TrackedObject {
    int id;
    cv::KalmanFilter kf;
    cv::Rect box;
    int lost_frames = 0;
    bool is_active = true;
    
    explicit TrackedObject(int id, const cv::Rect& b);
    void predict();
    void update(const cv::Rect& b);
};

class Tracker {
public:
    explicit Tracker(double distance_threshold = constants::TRACKING_DISTANCE_THRESHOLD,
                     int max_lost_frames = constants::MAX_LOST_FRAMES);
    
    void update(const std::vector<Detection>& detections);
    
    const std::vector<TrackedObject>& tracks() const { return tracks_; }
    std::vector<TrackedObject>& tracks() { return tracks_; }
    
    void setDistanceThreshold(double threshold) { distance_threshold_ = threshold; }
    void setMaxLostFrames(int frames) { max_lost_frames_ = frames; }
    
    void clear() { tracks_.clear(); next_id_ = 1; }

private:
    double distance_threshold_;
    int max_lost_frames_;
    int next_id_ = 1;
    std::vector<TrackedObject> tracks_;
    HungarianAlgorithm hungarian_;
};

} // namespace auto_aim
