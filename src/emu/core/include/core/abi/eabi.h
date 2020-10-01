/*
 * Copyright (c) 2018 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <string>
#include <typeinfo>

namespace eka2l1 {
    /*! \brief EABI emulation. 
        General uses for mangling name and pure virtual function. */
    namespace eabi {
        /*! Demangle an Itanium ABI name. */
        std::string demangle(std::string target);

        /*! Mangle a function name, should not use. */
        std::string mangle(std::string target);

        void pure_virtual_call();
        void deleted_virtual_call();

        void leave(uint32_t id, const std::string &msg);
        void trap_leave(uint32_t id);
    }
}