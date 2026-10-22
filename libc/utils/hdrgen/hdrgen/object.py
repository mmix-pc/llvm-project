# ====-- Object class for libc function headers ---------------*- python -*--==#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ==-------------------------------------------------------------------------==#

from hdrgen.symbol import Symbol, validate_guard


class Object(Symbol):
    def __init__(self, name, type, guard=None):
        super().__init__(name)
        self.type = type
        self.guard = validate_guard(guard)

    def __str__(self):
        split_type = self.type.partition("[")
        scalar_type = split_type[0]
        array_size = split_type[1] + split_type[2]
        return f"extern {scalar_type} {self.name}{array_size};"
