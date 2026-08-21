#pragma once
#include <cstdint>

namespace inhooks {

    // Хук CSGOInput::CreateMove (vtable 5) — канал записи углов для F1/F2.
    // Игра перезаписывает dwViewAngles каждый тик из юзеркоманды, поэтому
    // писать напрямую во viewAngles — проигрышная гонка. Правильно (как в
    // quintcs2): писать углы в текущий user cmd внутри CreateMove.
    // Возвращает false, если CSGOInput не найден (Vulkan-кейсы не мешают —
    // это ввод, не рендер; тут отказ = не нашлась глобалка).
    bool Init();
}
