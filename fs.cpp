#include <iostream>
#include "fs.h"
#include <string>
#include <cstring>
#include <vector>

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{

}

// formats the disk, i.e., creates an empty file system
int
FS::format()
{
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;
    for (int i = 2; i < BLOCK_SIZE/2; i++)
    {
        fat[i] = FAT_FREE;
    }

    dir root;
    root.size = 1;
    root.map[0] = true;
    for (int i = 1; i < DIR_ENTRY_MAX; i++)
    {
        root.map[i] = false;
    }

    root.entries[0].access_rights = (READ | WRITE |EXECUTE);
    strcpy(root.entries[0].file_name, "..");
    root.entries[0].type = TYPE_DIR;
    root.entries[0].first_blk = ROOT_BLOCK;

    uint8_t *bytes = reinterpret_cast<uint8_t*>(&root);

    disk.write(ROOT_BLOCK, bytes);

    shellDir = root;
    shellBlock = ROOT_BLOCK;
    shellPath.push_back("");

    std::cout << "Format complete\n";

    return 0;
}

bool FS::SplitPath(std::string filePath, std::vector<std::string> &tokens)
{
    std::string delimiter = "/";

    size_t pos = 0;
    std::string token;
    while ((pos = filePath.find(delimiter)) != std::string::npos) {

        token = filePath.substr(0, pos);

        tokens.push_back(token);

        filePath.erase(0, pos + delimiter.length());

        if (tokens[tokens.size() - 1].length() >= 56)
        {
            std::cout << "Error: entryname " << tokens[tokens.size() - 1] << " is too long" << std::endl;
            return 0;
        }
    }
    tokens.push_back(filePath);

    if (tokens.size() == 0)
    {
        std::cout << "Error: No filepath was given" << std::endl;
        return false;
    }

    return true;
}

int FS::FindEntry(dir *directory, std::string name)
{
    int index = 0;

    for (int i = 0; i < directory->size; i++)
    {
        while (directory->map[index] == false)
        {
            index++;
        }

        if (strcmp(name.c_str(), directory->entries[index].file_name) == 0)
        {
            return index;
        }
        index++;
    }
    return -1;
}

int FS::GetFreeBlock()
{
    for (int i = 2; i < BLOCK_SIZE/2; i++)
    {
        if (fat[i] == FAT_FREE)
        {
            fat[i] = FAT_EOF;
            return i;
        }
    }
    std::cout << "Error: No free blocks in disk" << std::endl;
    return -1;
}

bool FS::DirMarch(dir *directory, std::vector<std::string> &path, bool ignoreLast, int &dirBlockReturn)
{
    if (path.size() == 0)
    {
        std::cout << "Error: No path was given" << std::endl;
        return false;
    }

    int start = 0;
    if (path[0] == "")
    {
        disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(directory));
        dirBlockReturn = ROOT_BLOCK;
        start = 1;
    }
    else
    {
        disk.read(shellBlock, reinterpret_cast<uint8_t*>(directory));
        dirBlockReturn = shellBlock;
    }

    int steps = path.size() + (ignoreLast ? -1 : 0);

    for (int i = start; i < steps; i++)
    {
        int index = FindEntry(directory, path[i]);

        if (index == -1 || directory->entries[index].type == TYPE_FILE)
        {
            std::cout <<"Error: No such dirpath exists" << std::endl;
            return false;
        }
        if (!(directory->entries[index].access_rights & READ))
        {
            std::cout <<"Error: Does not have read access to " << directory->entries[index].file_name << std::endl;
            return false;
        }

        dirBlockReturn = directory->entries[index].first_blk;

        disk.read(directory->entries[index].first_blk, reinterpret_cast<uint8_t*>(directory));
    }

    return true;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int
FS::create(std::string filepath)
{
    std::vector<std::string> path;

    if (!SplitPath(filepath, path))
    {
        return 0;
    }

    uint8_t currentBlock[BLOCK_SIZE];

    dir *currentDir = reinterpret_cast<dir*>(currentBlock);
    int currentDirBlock;

    if (!DirMarch(currentDir, path, true, currentDirBlock))
    {
        return 0;
    }

    if (FindEntry(currentDir, path[path.size() - 1]) != -1)
    {
        std::cout << "Error: File already exists" << std::endl;
        return 0;
    }

    int index = 0;
    while (index < DIR_ENTRY_MAX && currentDir->map[index] != false)
    {
        index++;
    }
    if(index == DIR_ENTRY_MAX)
    {
        std::cout << "Error: Directory full" << std::endl;
        return 0;
    }

    currentDir->map[index] = true;
    currentDir->size++;

    strcpy(currentDir->entries[index].file_name, path[path.size() - 1].c_str());

    currentDir->entries[index].type = TYPE_FILE;
    currentDir->entries[index].access_rights = (READ | WRITE | EXECUTE);
    currentDir->entries[index].size = 0;

    int blockIndex = GetFreeBlock();
    if (blockIndex == -1)
    {
        return 0;
    }
    currentDir->entries[index].first_blk = blockIndex;


    std::string fileContent;
    std::string input;
    char byteArr[BLOCK_SIZE];

    int prevBlockIndex;

    bool loop = true;

    while (loop)
    {
        std::getline(std::cin, input);

        if (input == "")
        {
            loop = false;
        }
        else
        {
            fileContent.append(input);
            fileContent.append("\n");

            if ((fileContent.length() + 1) >= BLOCK_SIZE)
            {
                prevBlockIndex = blockIndex;
                blockIndex = GetFreeBlock();
                if (blockIndex == -1)
                {
                    break;
                }
                fat[prevBlockIndex] = blockIndex;

                strcpy(byteArr, fileContent.substr(0, BLOCK_SIZE - 1).c_str());

                disk.write(prevBlockIndex, reinterpret_cast<uint8_t*>(byteArr));

                fileContent.erase(0,BLOCK_SIZE - 1);

                currentDir->entries[index].size += BLOCK_SIZE;
            }
        }
    }

    if (fileContent != "" && blockIndex != -1)
    {
        strcpy(byteArr, fileContent.c_str());

        disk.write(blockIndex, reinterpret_cast<uint8_t*>(byteArr));

        currentDir->entries[index].size += fileContent.length() + 1;
    }

    disk.write(currentDirBlock, currentBlock);

    std::cout << "File save succesfull\n";

    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{
    std::vector<std::string> path;

    if (!SplitPath(filepath, path))
    {
        return 0;
    }

    uint8_t currentBlock[BLOCK_SIZE];

    dir *currentDir = reinterpret_cast<dir*>(currentBlock);
    int currentDirBlock;

    if (!DirMarch(currentDir, path, true, currentDirBlock))
    {
        return 0;
    }

    int index = FindEntry(currentDir, path[path.size() - 1]);

    if (index == -1)
    {
        std::cout << "Error: Filepath does not exist" << std::endl;
        return 0;
    }
    if (!(currentDir->entries[index].access_rights & READ))
    {
        std::cout << "Error: File has no read access rights" << std::endl;
        return 0;
    }
    if (currentDir->entries[index].type == TYPE_DIR)
    {
        std::cout << "Error: Target is a directory" << std::endl;
        return 0;
    }
    if (currentDir->entries[index].first_blk == -1)
    {
        std::cout << "Error: File has no content" << std::endl;
        return 0;
    }

    int blockIndex = currentDir->entries[index].first_blk;
    std::string fileContent;
    char fileBlock[BLOCK_SIZE];
    do
    {
        fileContent = "";
        disk.read(blockIndex, reinterpret_cast<uint8_t*>(fileBlock));
        fileContent.append(fileBlock);

        std::cout << fileContent;

        blockIndex = fat[blockIndex];

    } while (blockIndex != FAT_EOF);

    std::cout << std::endl;

    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    uint8_t currentBlock[BLOCK_SIZE];

    disk.read(shellBlock, currentBlock);

    dir *currentDir = reinterpret_cast<dir*>(currentBlock);

    std::cout << "name" << std::string(55, ' ') << "type    " << "accessrights  " << "size\n";

    for (int i = 0; i < DIR_ENTRY_MAX; i++)
    {
        if (currentDir->map[i])
        {
            std::cout << currentDir->entries[i].file_name
                    << std::string(59 - strlen(currentDir->entries[i].file_name), ' ');


            if (currentDir->entries[i].type == TYPE_FILE)
            {
                std::cout << "File    ";
            }
            else
            {
                std::cout << "Dir     ";
            }

            if (currentDir->entries[i].access_rights & READ)
            {
                std::cout << "r";
            }
            else
            {
                std::cout << "-";
            }
            if (currentDir->entries[i].access_rights & WRITE)
            {
                std::cout << "w";
            }
            else
            {
                std::cout << "-";
            }
            if (currentDir->entries[i].access_rights & EXECUTE)
            {
                std::cout << "x";
            }
            else
            {
                std::cout << "-";
            }

            std::cout << std::string(11, ' ');


            if (currentDir->entries[i].type == TYPE_FILE)
            {
                std::cout <<  currentDir->entries[i].size;
            }
            else
            {
                std::cout << "-";
            }
            std::cout << std::endl;
        }
    }

    return 0;
}

void FS::RemoveBlocks(int first)
{
    if (first == -1 || first == ROOT_BLOCK || first == FAT_BLOCK)
    {
        return;
    }
    RemoveBlocks(fat[first]);
    fat[first] = FAT_FREE;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    std::vector<std::string> pathSrc;

    if (!SplitPath(sourcepath, pathSrc))
    {
        return 0;
    }

    std::vector<std::string> pathDest;

    if (!SplitPath(destpath, pathDest))
    {
        return 0;
    }

    uint8_t blockSrc[BLOCK_SIZE];

    dir *srcDir = reinterpret_cast<dir*>(blockSrc);
    int notUsed;

    if (!DirMarch(srcDir, pathSrc, true, notUsed))
    {
        return 0;
    }

    int srcIndex = FindEntry(srcDir, pathSrc[pathSrc.size() - 1]);

    if (srcIndex == -1)
    {
        std::cout << "Error: Source filepath does not exist" << std::endl;
        return 0;
    }
    if (srcDir->entries[srcIndex].type == TYPE_DIR)
    {
        std::cout << "Error: Source is a directory" << std::endl;
        return 0;
    }
    if (!(srcDir->entries[srcIndex].access_rights & READ))
    {
        std::cout << "Error: Source has no read access" << std::endl;
        return 0;
    }

    uint8_t blockDest[BLOCK_SIZE];

    dir *destDir = reinterpret_cast<dir*>(blockDest);
    int currentDirBlock;

    if (!DirMarch(destDir, pathDest, true, currentDirBlock))
    {
        return 0;
    }

    if (FindEntry(destDir, pathDest[pathDest.size() - 1]) != -1)
    {
        std::cout << "Error: Destfile already exists" << std::endl;
        return 0;
    }

    int destIndex = 0;
    while (destIndex < DIR_ENTRY_MAX && destDir->map[destIndex] != false)
    {
        destIndex++;
    }
    if(destIndex == DIR_ENTRY_MAX)
    {
        std::cout << "Error: Directory full" << std::endl;
        return 0;
    }

    destDir->map[destIndex] = true;
    destDir->size++;

    strcpy(destDir->entries[destIndex].file_name, pathDest[pathDest.size() - 1].c_str());
    destDir->entries[destIndex].type = TYPE_FILE;
    destDir->entries[destIndex].size = srcDir->entries[srcIndex].size;
    destDir->entries[destIndex].access_rights = srcDir->entries[srcIndex].access_rights;

    if (srcDir->entries[srcIndex].first_blk > 2)
    {
        std::cout << "Error: source has no content to copy" << std::endl;
        return 0;
    }

    int srcBlock = srcDir->entries[srcIndex].first_blk;
    int destBlock;
    uint8_t block[BLOCK_SIZE];

    destBlock = GetFreeBlock();
    if (destBlock == -1)
    {
        return 0;
    }
    destDir->entries[destIndex].first_blk = destBlock;

    disk.read(srcBlock, block);
    disk.write(destBlock, block);

    srcBlock = fat[srcBlock];

    while (srcBlock != FAT_EOF)
    {
        fat[destBlock] = GetFreeBlock();
        destBlock = fat[destBlock];
        if (destBlock == -1)
        {
            RemoveBlocks(destDir->entries[destIndex].first_blk);
            return 0;
        }

        disk.read(srcBlock, block);
        disk.write(destBlock, block);

        srcBlock = fat[srcBlock];
    }

    disk.write(currentDirBlock, blockDest);

    std::cout << "File copy succesfull\n";

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    std::vector<std::string> pathSrc;

    if (!SplitPath(sourcepath, pathSrc))
    {
        return 0;
    }

    std::vector<std::string> pathDest;

    if (!SplitPath(destpath, pathDest))
    {
        return 0;
    }

    uint8_t blockSrc[BLOCK_SIZE];

    dir *srcDir = reinterpret_cast<dir*>(blockSrc);
    int srcDirBlock;

    if (!DirMarch(srcDir, pathSrc, true, srcDirBlock))
    {
        return 0;
    }

    int srcIndex = FindEntry(srcDir, pathSrc[pathSrc.size() - 1]);

    if (srcIndex == -1)
    {
        std::cout << "Error: Source filepath does not exist" << std::endl;
        return 0;
    }
    if (!(srcDir->entries[srcIndex].access_rights & READ))
    {
        std::cout << "Error: Source has no read access" << std::endl;
        return 0;
    }

    uint8_t blockDest[BLOCK_SIZE];

    dir *destDir = reinterpret_cast<dir*>(blockDest);
    int destDirBlock;

    if (destpath == "")
    {
        disk.read(ROOT_BLOCK, blockDest);
        destDirBlock = ROOT_BLOCK;
    }
    else
    {
        if (!DirMarch(destDir, pathDest, true, destDirBlock))
        {
            return 0;
        }
    }
    bool rename = true;
    int destIndex = FindEntry(destDir, pathDest[pathDest.size() - 1]);

    if (destIndex != -1)
    {
        if (destDir->entries[destIndex].type == TYPE_DIR)
        {
            rename = false;
            destDirBlock = destDir->entries[destIndex].first_blk;
            disk.read(destDirBlock, blockDest);

            if (FindEntry(destDir, srcDir->entries[srcIndex].file_name) != -1)
            {
                std::cout << "Error: file of same name already exists in dest" << std::endl;
            }
        }
    }
    else if (pathDest[pathDest.size() - 1] == "")
    {
        rename = false;
        if (FindEntry(destDir, srcDir->entries[srcIndex].file_name) != -1)
        {
            std::cout << "Error: file of same name already exists in dest" << std::endl;
        }
    }

    if (rename && FindEntry(destDir, pathDest[pathDest.size() - 1]) != -1)
            {
                std::cout << "Error: file of same name already exists in dest" << std::endl;
            }

    destIndex = 0;
    while (destIndex < DIR_ENTRY_MAX && destDir->map[destIndex] != false)
    {
        destIndex++;
    }
    if(destIndex == DIR_ENTRY_MAX)
    {
        std::cout << "Error: Directory full" << std::endl;
        return 0;
    }

    if (srcDirBlock == destDirBlock)
    {
        destDir->map[srcIndex] = false;
        destDir->size--;
    }
    else
    {
        srcDir->map[srcIndex] = false;
        srcDir->size--;
    }
    

    destDir->map[destIndex] = true;
    destDir->size++;

    if (rename)
    {
        strcpy(destDir->entries[destIndex].file_name, pathDest[pathDest.size() - 1].c_str());
    }
    else
    {
        strcpy(destDir->entries[destIndex].file_name, srcDir->entries[srcIndex].file_name);
    }
    
    destDir->entries[destIndex].type = srcDir->entries[srcIndex].type;
    destDir->entries[destIndex].size = srcDir->entries[srcIndex].size;
    destDir->entries[destIndex].access_rights = srcDir->entries[srcIndex].access_rights;
    destDir->entries[destIndex].first_blk = srcDir->entries[srcIndex].first_blk;

    if (srcDirBlock != destDirBlock)
    {
        disk.write(srcDirBlock, blockSrc);
    }
    disk.write(destDirBlock, blockDest);

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    std::vector<std::string> path;

    if (!SplitPath(filepath, path))
    {
        return 0;
    }

    uint8_t currentBlock[BLOCK_SIZE];

    dir *currentDir = reinterpret_cast<dir*>(currentBlock);
    int currentDirBlock;

    if (!DirMarch(currentDir, path, true, currentDirBlock))
    {
        return 0;
    }

    int index = FindEntry(currentDir, path[path.size() - 1]);

    if (index == -1)
    {
        std::cout << "Error: Filepath does not exist" << std::endl;
        return 0;
    }
    if (!(currentDir->entries[index].access_rights & WRITE))
    {
        std::cout << "Error: You have no write/edit right over the file" << std::endl;
        return 0;
    }

    if (currentDir->entries[index].type == TYPE_DIR)
    {
        uint8_t testBlock[BLOCK_SIZE];
        disk.read(currentDir->entries[index].first_blk, testBlock);

        dir *test = reinterpret_cast<dir*>(testBlock);

        if (test->size > 1)
        {
            std::cout << "Error: Can't remove nonempty directory" << std::endl;
            return 0;
        }
    }

    if (currentDir->entries[index].first_blk > FAT_BLOCK)
    {
        RemoveBlocks(currentDir->entries[index].first_blk);
    }

    currentDir->size--;
    currentDir->map[index] = false;

    disk.write(currentDirBlock, currentBlock);

    std::cout << "Deletion complete" << std::endl;

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    std::vector<std::string> pathSrc;

    if (!SplitPath(filepath1, pathSrc))
    {
        return 0;
    }

    std::vector<std::string> pathDest;

    if (!SplitPath(filepath2, pathDest))
    {
        return 0;
    }

    uint8_t blockSrc[BLOCK_SIZE];

    dir *srcDir = reinterpret_cast<dir*>(blockSrc);
    int notUsed;

    if (!DirMarch(srcDir, pathSrc, true, notUsed))
    {
        return 0;
    }

    int srcIndex = FindEntry(srcDir, pathSrc[pathSrc.size() - 1]);

    if (srcIndex == -1)
    {
        std::cout << "Error: Filepath1 does not exist" << std::endl;
        return 0;
    }
    if (srcDir->entries[srcIndex].type == TYPE_DIR)
    {
        std::cout << "Error: Filepath1 is a directory" << std::endl;
        return 0;
    }
    if (!(srcDir->entries[srcIndex].access_rights & READ))
    {
        std::cout << "Error: Filepath1 has no read access" << std::endl;
        return 0;
    }

    uint8_t blockDest[BLOCK_SIZE];

    dir *destDir = reinterpret_cast<dir*>(blockDest);
    int destDirBlock;

    if (!DirMarch(destDir, pathDest, true, destDirBlock))
    {
        return 0;
    }

    int destIndex = FindEntry(destDir, pathDest[pathDest.size() - 1]);

    if (destIndex == -1)
    {
        std::cout << "Error: filepath2 does not exist" << std::endl;
        return 0;
    }
    if (destDir->entries[destIndex].type == TYPE_DIR)
    {
        std::cout << "Error: filepath2 is a directory" << std::endl;
        return 0;
    }
    if (!(destDir->entries[destIndex].access_rights & WRITE))
    {
        std::cout << "Error: filepath2 has no read access" << std::endl;
        return 0;
    }

    if (!(srcDir->entries[srcIndex].first_blk > 1))
    {
        std::cout << "Error: filepath2 has no content to copy" << std::endl;
        return 0;
    }

    destDir->entries[destIndex].size += srcDir->entries[srcIndex].size - 1;
    disk.write(destDirBlock, blockDest);

    int srcBlock = srcDir->entries[srcIndex].first_blk;
    int destBlock = destDir->entries[destIndex].first_blk;
    while (fat[destBlock] != FAT_EOF)
    {
        destBlock = fat[srcBlock];
    }

    char *srcContent = reinterpret_cast<char*>(blockSrc);
    char *destContent = reinterpret_cast<char*>(blockDest);

    std::string buffer;

    while (srcBlock != FAT_EOF)
    {
        disk.read(srcBlock, blockSrc);
        disk.read(destBlock, blockDest);
        buffer.append(destContent);
        buffer.append(srcContent);

        if ((buffer.length() + 1 > BLOCK_SIZE))
        {
            strcpy(destContent, buffer.substr(0, BLOCK_SIZE - 1).c_str());
            buffer.erase(0, BLOCK_SIZE - 1);
            disk.write(destBlock, blockDest);

            fat[destBlock] = GetFreeBlock();
            destBlock = fat[destBlock];
            if (destBlock == -1)
            {
                std::cout << "Error: No free blocks for append" << std::endl;
                return 0;
            }
        }
        strcpy(destContent, buffer.c_str());
        disk.write(destBlock, blockDest);
        srcBlock = fat[srcBlock];
    }

    std::cout << "File append succesfull\n";

    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    std::vector<std::string> path;

    if (!SplitPath(dirpath, path))
    {
        return 0;
    }

    uint8_t currentBlock[BLOCK_SIZE];

    dir *currentDir = reinterpret_cast<dir*>(currentBlock);
    int currentDirBlock;

    if (!DirMarch(currentDir, path, true, currentDirBlock))
    {
        return 0;
    }

    if (FindEntry(currentDir, path[path.size() - 1]) != -1)
    {
        std::cout << "Error: Directory already exists" << std::endl;
        return 0;
    }

    int index = 0;
    while (index < DIR_ENTRY_MAX && currentDir->map[index] != false)
    {
        index++;
    }
    if(index == DIR_ENTRY_MAX)
    {
        std::cout << "Error: Directory full" << std::endl;
        return 0;
    }

    currentDir->map[index] = true;
    currentDir->size++;

    strcpy(currentDir->entries[index].file_name, path[path.size() - 1].c_str());
    currentDir->entries[index].type = TYPE_DIR;
    currentDir->entries[index].access_rights = (READ | WRITE | EXECUTE);

    int blockIndex = GetFreeBlock();
    if (blockIndex == -1)
    {
        return 0;
    }
    currentDir->entries[index].first_blk = blockIndex;

    uint8_t newBlock[BLOCK_SIZE];
    dir *newDir = reinterpret_cast<dir*>(newBlock);
    newDir->map[0] = true;
    for (int i = 1; i < DIR_ENTRY_MAX; i++)
    {
        newDir->map[i] = false;
    }
    strcpy(newDir->entries[0].file_name, "..");
    newDir->entries[0].type = TYPE_DIR;
    newDir->entries[0].access_rights = (READ | WRITE | EXECUTE);
    newDir->entries[0].first_blk = currentDirBlock;
    newDir->size = 1;

    disk.write(blockIndex, newBlock);

    disk.write(currentDirBlock, currentBlock);

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    std::vector<std::string> path;

    if (!SplitPath(dirpath, path))
    {
        return 0;
    }

    dir currentDir;
    int currentDirBlock;

    if (dirpath == "/")
    {
        disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t*>(&currentDir));
        currentDirBlock = ROOT_BLOCK;
    }
    else
    {
        if (!DirMarch(&currentDir, path, false, currentDirBlock))
        {
            return 0;
        }
    }


    shellDir = currentDir;
    shellBlock = currentDirBlock;

    if (path[0] == "")
    {
        shellPath.clear();
    }

    for (std::string entry : path)
    {
        if (entry == "..")
        {
            if (shellPath.size() > 1)
            {
                shellPath.pop_back();
            }
        }
        else
        {
            shellPath.push_back(entry);
        }
    }
    if (shellPath[shellPath.size() - 1] == "" && shellPath.size() > 1)
    {
        shellPath.pop_back();
    }

    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    for(std::string entry : shellPath)
    {
        std::cout << entry << "/";
    }
    std::cout << std::endl;
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    std::vector<std::string> path;

    if (!SplitPath(filepath, path))
    {
        return 0;
    }

    uint8_t currentBlock[BLOCK_SIZE];

    dir *currentDir = reinterpret_cast<dir*>(currentBlock);
    int currentDirBlock;

    if (!DirMarch(currentDir, path, true, currentDirBlock))
    {
        return 0;
    }

    int index = FindEntry(currentDir, path[path.size() - 1]);

    if (index == -1)
    {
        std::cout << "Error: Filepath does not exist" << std::endl;
        return 0;
    }

    currentDir->entries[index].access_rights = std::stoi(accessrights);

    disk.write(currentDirBlock, currentBlock);
    return 0;
}
