#include "fuzzy_pid.hpp"
#include <cmath>

namespace auto_aim {

FuzzyPID::FuzzyPID(double kp, double ki, double kd)
    : kp_base_(kp), ki_base_(ki), kd_base_(kd) {}

void FuzzyPID::reset() {
    integral_x_ = 0;
    integral_y_ = 0;
    prev_err_x_ = 0;
    prev_err_y_ = 0;
}

double FuzzyPID::getFuzzyKp(double e, double de) const {
    (void)de;
    double abs_e = std::abs(e);
    if (abs_e > constants::FUZZY_KP_THRESHOLD_HIGH) 
        return kp_base_ * constants::FUZZY_KP_MULTIPLIER_HIGH;
    if (abs_e > constants::FUZZY_KP_THRESHOLD_MID) 
        return kp_base_ * constants::FUZZY_KP_MULTIPLIER_MID;
    return kp_base_;
}

double FuzzyPID::getFuzzyKd(double e, double de) const {
    (void)de;
    double abs_e = std::abs(e);
    if (abs_e < constants::FUZZY_KD_THRESHOLD) 
        return kd_base_ * constants::FUZZY_KD_MULTIPLIER;
    return kd_base_;
}

void FuzzyPID::compute(double err_x, double err_y, double dt, 
                       double& out_x, double& out_y) {
    if (dt <= 0.0) dt = 0.01;
    
    double de_x = (err_x - prev_err_x_) / dt;
    double de_y = (err_y - prev_err_y_) / dt;
    
    integral_x_ += err_x * dt;
    integral_y_ += err_y * dt;
    
    double kp_x = getFuzzyKp(err_x, de_x);
    double kd_x = getFuzzyKd(err_x, de_x);
    double kp_y = getFuzzyKp(err_y, de_y);
    double kd_y = getFuzzyKd(err_y, de_y);
    
    out_x = kp_x * err_x + ki_base_ * integral_x_ + kd_x * de_x;
    out_y = kp_y * err_y + ki_base_ * integral_y_ + kd_y * de_y;
    
    prev_err_x_ = err_x;
    prev_err_y_ = err_y;
}

void FuzzyPID::setGains(double kp, double ki, double kd) {
    kp_base_ = kp;
    ki_base_ = ki;
    kd_base_ = kd;
}

} // namespace auto_aim
