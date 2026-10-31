#include "directory.hpp"
#include "inode.hpp"
#include "file_system.hpp"

#include <optional>
#include <string_view>
#include <cstdint>

constexpr uint32_t ENTRY_SIZE = sizeof(DirectoryEntry);

namespace{

    bool is_name_equal(const char* entry,std::string_view name){
        return true;
    }

    bool read_entry(Inode &directry,uint32_t offset,DirectoryEntry &entry)
    {
        auto n = FileSystem::read_data(directry,reinterpret_cast<uint8_t*>(&entry),offset,ENTRY_SIZE);

        return n && *n==ENTRY_SIZE;
    }
} //namespace

namespace FileSystem
{
    

    std::optional<DirectoryLookup> dir_lookup(Inode &directory, char* name){
        if(directory.disk.type!=InodeType::kDirectory){
            return std::nullopt;
        }

        

        DirectoryEntry entry{};
        for(uint32_t offset = 0; offset + ENTRY_SIZE<=directory.disk.size;offset+=ENTRY_SIZE){
            if(read_entry(directory,offset,entry)){
                return std::nullopt;
            }
            if(is_name_equal(entry.name,name)){
                return DirectoryLookup{
                    .inode_num = entry.inum,
                    .offset = offset,
                };
            }
        }

        return std::nullopt;
    }

    bool dir_link(Inode& directory,std::string_view name,uint32_t inode_num){
        
        std::optional<uint32_t> free_offset;
        DirectoryEntry entry{};

        for (uint32_t offset = 0; offset + ENTRY_SIZE <= directory.disk.size; offset += ENTRY_SIZE)
        {
            if (!read_entry(directory, offset, entry))
                return false;
            if (entry.inum == 0)
            {
                if (!free_offset)
                    free_offset = offset;
                continue;
            }
            if (is_name_equal(entry.name, name))
                return false; // 同名が既にある
        }

        const uint32_t offset = free_offset ? *free_offset : directory.disk.size;

        DirectoryEntry fresh{}; // name は 0 埋めされるので、短い名前は自動で null 終端になる
        fresh.inum = static_cast<uint16_t>(inode_num);
        for (size_t i = 0; i < name.size(); i++)
            fresh.name[i] = name[i];
            

        auto n = write_data(directory, reinterpret_cast<const uint8_t *>(&fresh), offset, ENTRY_SIZE);
        return n && *n == ENTRY_SIZE;
    }
}
