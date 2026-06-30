#pragma once

#include <string>

const wchar_t* gap_details_title_text(bool english);
std::wstring gap_details_body_text(
    bool english,
    double duration,
    long long estimated_missing_samples,
    double reference_step);
long long gap_estimated_missing_samples(double duration, double gap_step);
double gap_reference_step_from_estimate(double duration, long long estimated_missing_samples);
