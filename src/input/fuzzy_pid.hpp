#pragma once
#include "utils/constants.hpp"

namespace auto_aim {

class FuzzyPID {
public:
    explicit FuzzyPID(double kp = constants::PID_KP_BASE, 
                      double ki = constants::PID_KI_BASE, 
                      double kd = constants::PID_KD_BASE);
    
    void reset();
    void compute(double err_x, double err_y, double dt, 
                 double& out_x, double& out_y);
    
    double getKp() const { return kp_base_; }
    double getKi() const { return ki_base_; }
    double getKd() const { return kd_base_; }
    
    void setGains(double kp, double ki, double kd);

private:
    double getFuzzyKp(double e, double de) const;
    double getFuzzyKd(double e, double de) const;
    
    double kp_base_, ki_base_, kd_base_;
    double integral_x_ = 0, integral_y_ = 0;
    double prev_err_x_ = 0, prev_err_y_ = 0;
};

} // namespace auto_aim
