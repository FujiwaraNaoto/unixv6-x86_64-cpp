#ifndef DIRECTORY_HPP
#define DIRECTORY_HPP

#include "inode.hpp"

#include <cstdint>
#include <optional>

namespace FileSystem
{
    struct DirectoryLookup
    {
        uint32_t inode_num;
        uint32_t offset;
    };

    std::optional<DirectoryLookup> dir_lookup(Inode &dir, char* name);


}

#endif
