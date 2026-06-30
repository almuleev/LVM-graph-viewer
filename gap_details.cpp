#include "gap_details.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

const wchar_t* gap_details_title_text(bool english) {
    return english ? L"Gap details" : L"Информация о разрыве";
}

long long gap_estimated_missing_samples(double duration, double gap_step) {
    if (!(duration > 0.0) || !(gap_step > 0.0) || !std::isfinite(duration) || !std::isfinite(gap_step)) {
        return 0;
    }
    long long estimated = static_cast<long long>(std::llround(duration / gap_step)) - 1;
    return std::max<long long>(0, estimated);
}

double gap_reference_step_from_estimate(double duration, long long estimated_missing_samples) {
    if (!(duration > 0.0) || estimated_missing_samples <= 0 || !std::isfinite(duration)) {
        return 0.0;
    }
    return duration / static_cast<double>(estimated_missing_samples + 1);
}

std::wstring gap_details_body_text(
    bool english,
    double duration,
    long long estimated_missing_samples,
    double reference_step) {
    wchar_t buf[256]{};
    std::wstring out;
    if (english) {
        swprintf(buf, 256, L"Gap duration: %.6g s\n", duration);
        out += buf;
        swprintf(buf, 256, L"Approx. missing samples: ~%lld\n", estimated_missing_samples);
        out += buf;
        if (reference_step > 0.0) {
            swprintf(buf, 256, L"Reference step: %.6g s\n\n", reference_step);
        } else {
            swprintf(buf, 256, L"Reference step: unavailable\n\n");
        }
        out += buf;
        out += L"A gap is a pause between neighboring samples that is much larger than the usual step.\n";
        out += L"The estimate uses round(duration / step) - 1, so it is approximate.";
    } else {
        swprintf(buf, 256, L"Длительность разрыва: %.6g c\n", duration);
        out += buf;
        swprintf(buf, 256, L"Пропущено примерно: ~%lld отсч.\n", estimated_missing_samples);
        out += buf;
        if (reference_step > 0.0) {
            swprintf(buf, 256, L"Типичный шаг: %.6g c\n\n", reference_step);
        } else {
            swprintf(buf, 256, L"Типичный шаг: не удалось оценить\n\n");
        }
        out += buf;
        out += L"Разрыв — это промежуток между соседними отсчётами, который заметно больше обычного.\n";
        out += L"Оценка использует round(duration / step) - 1, поэтому она приблизительная.";
    }
    return out;
}
