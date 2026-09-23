#pragma once

#include <cstdint>

struct MatrixPosition
{
    uint8_t row;
    uint8_t column;
};

bool crosspointInit();

bool crosspointPress(const MatrixPosition &position);
void crosspointRelease(const MatrixPosition &position);

void crosspointReleaseAll();
