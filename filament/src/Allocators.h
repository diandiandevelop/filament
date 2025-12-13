/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TNT_FILAMENT_DETAILS_ALLOCATORS_H
#define TNT_FILAMENT_DETAILS_ALLOCATORS_H

#include <utils/Allocator.h>

#include "private/backend/BackendUtils.h"

namespace filament {

#ifndef NDEBUG

/**
 * 堆分配器竞技场（调试版本）
 * 
 * 在调试构建中，HeapAllocatorArena 需要 LockingPolicy::Mutex，
 * 因为它使用了 TrackingPolicy，需要同步。
 * 
 * 配置：
 * - 堆分配器：使用 malloc/free
 * - 互斥锁策略：需要同步（因为跟踪策略）
 * - 跟踪策略：调试和高水位标记跟踪
 * - 区域策略：空区域（不使用预分配区域）
 */
using HeapAllocatorArena = utils::Arena<
        utils::HeapAllocator,  // 堆分配器
        utils::LockingPolicy::Mutex,  // 互斥锁策略（调试版本需要）
        utils::TrackingPolicy::DebugAndHighWatermark,  // 调试和高水位标记跟踪
        utils::AreaPolicy::NullArea>;  // 空区域策略

/**
 * 线性分配器竞技场（调试版本）
 * 
 * 使用调试和高水位标记跟踪策略。
 * 
 * 配置：
 * - 线性分配器：连续内存分配
 * - 无锁策略：单线程使用
 * - 跟踪策略：调试和高水位标记跟踪
 */
using LinearAllocatorArena = utils::Arena<
        utils::LinearAllocator,  // 线性分配器
        utils::LockingPolicy::NoLock,  // 无锁策略
        utils::TrackingPolicy::DebugAndHighWatermark>;  // 调试和高水位标记跟踪

#else

/**
 * 堆分配器竞技场（发布版本）
 * 
 * 在发布构建中，HeapAllocatorArena 不需要 LockingPolicy，
 * 因为 HeapAllocator 本身是同步的（它依赖于堆分配，即 malloc/free）。
 * 
 * 配置：
 * - 堆分配器：使用 malloc/free
 * - 无锁策略：HeapAllocator 本身是同步的
 * - 无跟踪策略：不跟踪分配（性能最优）
 * - 区域策略：空区域（不使用预分配区域）
 */
using HeapAllocatorArena = utils::Arena<
        utils::HeapAllocator,  // 堆分配器
        utils::LockingPolicy::NoLock,  // 无锁策略（HeapAllocator 本身同步）
        utils::TrackingPolicy::Untracked,  // 无跟踪策略（性能最优）
        utils::AreaPolicy::NullArea>;  // 空区域策略

/**
 * 线性分配器竞技场（发布版本）
 * 
 * 不使用跟踪策略，性能最优。
 * 
 * 配置：
 * - 线性分配器：连续内存分配
 * - 无锁策略：单线程使用
 * - 无跟踪策略：不跟踪分配（性能最优）
 */
using LinearAllocatorArena = utils::Arena<
        utils::LinearAllocator,  // 线性分配器
        utils::LockingPolicy::NoLock>;  // 无锁策略（无跟踪策略，性能最优）

#endif

/**
 * 根竞技场作用域
 * 
 * 使用线性分配器竞技场的作用域管理。
 */
using RootArenaScope = utils::ArenaScope<LinearAllocatorArena>;

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_ALLOCATORS_H
