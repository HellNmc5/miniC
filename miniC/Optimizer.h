#pragma once
#include <vector>
#include "Triads.h"

/**
 * @brief Свёртка констант: триады, результат которых известен при компиляции,
 *        заменяются на C значение, а ссылки на них — самим значением.
 * @param code список триад; изменяется на месте, номера триад сохраняются
 * @return сколько триад свёрнуто
 */
int foldConstants(std::vector<Triad>& code);