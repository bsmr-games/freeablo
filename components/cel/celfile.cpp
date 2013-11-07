#include <stdio.h>
#include <stdint.h>

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>


#include "celfile.h"
#include "cel_frame.h"



Cel_file::Cel_file(std::string filename)
{
    mFile = fopen(filename.c_str(), "rb");

    mPal = new colour[256];
    get_pallette(filename, mPal);

    mFrame_offsets.resize(read_num_frames()+1); // +1 so we can put in the end offset, too
    read_frame_offsets();
    
    mIs_tile_cel = is_tile_cel(filename);
}

size_t Cel_file::num_frames()
{
    return mFrame_offsets.size() -1;
}

Cel_frame& Cel_file::operator[] (size_t index)
{
    if(mCache.count(index))
        return mCache[index];


    Cel_frame frame;
    std::vector<colour> raw_image;
    frame.raw_image = raw_image;
    frame.width = get_frame(index, frame.raw_image);
    frame.height = frame.raw_image.size() / frame.width;

    mCache[index] = frame;
    
    #ifdef CEL_DEBUG
        std::cout << "w: " << frame.width << ", h: " << frame.height << std::endl;
    #endif

    return mCache[index];
}

size_t Cel_file::get_frame(size_t frame_num, std::vector<colour>& raw_image)
{
    size_t frame_size = mFrame_offsets[frame_num+1] - mFrame_offsets[frame_num];

    #ifdef CEL_DEBUG
        std::cout << std::endl << "frame 0 size: " << frame_size << std::endl;
    #endif

    

    // Load frame data
	uint8_t* frame = new uint8_t[frame_size];
    //uint8_t frame[frame_size];
    fseek(mFile, mFrame_offsets[frame_num], SEEK_SET);
    fread(frame, 1, frame_size, mFile);

    // Make sure we're not concatenating onto some other image 
    raw_image.clear();
    

    //print_cel(frame, frame_size);
    //std::cout << std::endl;
   
    size_t width;
    
    if(mIs_tile_cel)
        width = 32;

    if(mIs_tile_cel && is_less_than(frame, frame_size))
        decode_less_than(frame, frame_size, mPal, raw_image);
    
    else if(mIs_tile_cel && is_greater_than(frame, frame_size))
        decode_greater_than(frame, frame_size, mPal, raw_image);
    
    else
    {
        uint16_t offset;
        bool from_header = false;
        
        // Tile cel frames never have headers 
        if(!mIs_tile_cel)
        { 
            // The frame has a header which we can use to determine width
            if(frame[0] == 10)
            {
                from_header = true;
                fseek(mFile, mFrame_offsets[frame_num]+2, SEEK_SET);
                fread(&offset, 2, 1, mFile);
            }
            
            width = normal_width(frame, frame_size, from_header, offset);
        }
        
        normal_decode(frame, frame_size, width, from_header, mPal, raw_image);
        
        #ifdef CEL_DEBUG
            std::cout << raw_image.size() << " " << frame_size << std::endl;
        #endif
        
        if(mIs_tile_cel && frame_size == 1024 && frame_num != 2593) // It's a fully opaque raw frame, width 32, from a level tileset
        //if(mIs_tile_cel && raw_image.size() < frame_size ) // It's a fully opaque raw frame, width 32, from a level tileset
        {
            raw_image.clear();
            decode_raw_32(frame, frame_size, mPal, raw_image);
        }
        

    }
    
    #ifdef CEL_DEBUG 
        std::cout << "WIDTH used: " << width << std::endl;
    #endif


    return width;
}

size_t Cel_file::read_num_frames()
{
    fseek(mFile, 0, SEEK_SET);

    uint32_t num_frames;
    
    fread(&num_frames, 4, 1, mFile);

    #ifdef CEL_DEBUG
        std::cout << ftell(mFile) << ": Num frames: " << num_frames << std::endl;
    #endif

    return num_frames;
}

void Cel_file::read_frame_offsets()
{
    fseek(mFile, 4, SEEK_SET);

    for(size_t i = 0; i < num_frames(); i++){
            fread(&mFrame_offsets[i], 4, 1, mFile);
            //std::cout << ftell(mFile) << ": offset " << i << ": " << mFrame_offsets[i] << std::endl;
    }

    fread(&mFrame_offsets[num_frames()], 4, 1, mFile);

    #ifdef CEL_DEBUG
        std::cout << ftell(mFile) << ": end offset: " << mFrame_offsets[num_frames()] << std::endl;
    #endif
}

bool Cel_file::greater_than_first(uint8_t* frame, size_t frame_size)
{
    return frame_size >= 196 &&
    frame[  2] == 0 && frame[  3] == 0 &&
    frame[ 14] == 0 && frame[ 15] == 0 &&
    frame[ 34] == 0 && frame[ 35] == 0 &&
    frame[ 62] == 0 && frame[ 63] == 0 &&
    frame[ 98] == 0 && frame[ 99] == 0 &&
    frame[142] == 0 && frame[143] == 0 &&
    frame[194] == 0 && frame[195] == 0;
}

bool Cel_file::greater_than_second(uint8_t* frame, size_t frame_size)
{
    return frame_size >= 196 &&
    frame[254] == 0 && frame[255] == 0 &&
    frame[318] == 0 && frame[319] == 0 &&
    frame[374] == 0 && frame[375] == 0 &&
    frame[422] == 0 && frame[423] == 0 &&
    frame[462] == 0 && frame[463] == 0 &&
    frame[494] == 0 && frame[495] == 0 &&
    frame[518] == 0 && frame[519] == 0 &&
    frame[534] == 0 && frame[535] == 0;
}

bool Cel_file::is_greater_than(uint8_t* frame, size_t frame_size)
{
    return greater_than_first(frame, frame_size);
}

void Cel_file::drawRow(int row, int lastRow, int& framePos, uint8_t* frame, colour* pal, std::vector<colour>& raw_image, bool lessThan)
{
    for(; row < lastRow; row++)
    {
        // Skip markers - for less than, when on the first half of the image (row < 16), all even rows will start with a pair of marker bits
        // for the second half of the image (row >= 16), all odd rows will start with a pair of marker bits.
        // The inverse is true of greater than images.
        if( (lessThan && ((row < 16 && row % 2 == 0) || (row >= 16 && row % 2 != 0))) ||
           (!lessThan && ((row < 16 && row % 2 != 0) || (row >= 16 && row % 2 == 0))))
            framePos += 2;
        
        int toDraw;
        if(row < 16)
            toDraw = 2 + (row * 2);
        else
            toDraw = 32 - ((row - 16) * 2);
        
        #ifdef CEL_DEBUG
            std::cout << "\trow: " << row << ", draw: " << toDraw << std::endl;
        #endif

        if(lessThan)
            fill_t(32-toDraw, raw_image);

        for(int px = 0; px < toDraw; px++)
        {
                raw_image.push_back(pal[frame[framePos]]);
                framePos++;
        }

        if(!lessThan)
            fill_t(32-toDraw, raw_image);
        
        #ifdef CEL_DEBUG
            std::cout << "len: " << raw_image.size() << std::endl;
        #endif
    }
}

void Cel_file::decode_greater_less_than(uint8_t* frame, size_t frame_size, colour* pal, std::vector<colour>& raw_image, bool lessThan)
{
    #ifdef CEL_DEBUG
        std::cout << (lessThan ? "Less" : "Greater") << " than" << std::endl;
    #endif

    int framePos = 0;
    
    drawRow(0, 15, framePos, frame, pal, raw_image, lessThan);

    
    if((lessThan && less_than_second(frame, frame_size)) || (!lessThan && greater_than_second(frame, frame_size)))
    {
        drawRow(16, 33, framePos, frame, pal, raw_image, lessThan);
    }
    else
    {
        for(framePos = 256; framePos < frame_size; framePos++)
            raw_image.push_back(pal[frame[framePos]]);
    }
    
    #ifdef CEL_DEBUG
        std::cout << (lessThan ? "LT" : "GT") << raw_image.size() << std::endl;
    #endif

}

void Cel_file::decode_greater_than(uint8_t* frame, size_t frame_size, colour* pal, std::vector<colour>& raw_image)
{
    decode_greater_less_than(frame, frame_size, pal, raw_image, false);
}

void Cel_file::decode_less_than(uint8_t* frame, size_t frame_size, colour* pal, std::vector<colour>& raw_image)
{
    decode_greater_less_than(frame, frame_size, pal, raw_image, true);
}



bool Cel_file::less_than_first(uint8_t* frame, size_t frame_size)
{
    return frame_size >= 226 &&
    frame[  0] == 0 && frame[  1] == 0 &&
    frame[  8] == 0 && frame[  9] == 0 &&
    frame[ 24] == 0 && frame[ 25] == 0 &&
    frame[ 48] == 0 && frame[ 49] == 0 &&
    frame[ 80] == 0 && frame[ 81] == 0 &&
    frame[120] == 0 && frame[121] == 0 &&
    frame[168] == 0 && frame[169] == 0 &&
    frame[224] == 0 && frame[225] == 0;
}

bool Cel_file::less_than_second(uint8_t* frame, size_t frame_size)
{
    return frame_size >= 530 &&
    frame[288] == 0 && frame[289] == 0 &&
    frame[348] == 0 && frame[349] == 0 &&
    frame[400] == 0 && frame[401] == 0 &&
    frame[444] == 0 && frame[445] == 0 &&
    frame[480] == 0 && frame[481] == 0 &&
    frame[508] == 0 && frame[509] == 0 &&
    frame[528] == 0 && frame[529] == 0;
}

bool Cel_file::is_less_than(uint8_t* frame, size_t frame_size)
{
    return less_than_first(frame, frame_size);
}

void Cel_file::get_pal(std::string pal_filename, colour* pal)
{
    FILE * pal_file;

    pal_file = fopen(pal_filename.c_str(), "rb");
    
    for(int i = 0; i < 256; i++)
    {
            fread(&pal[i].r, 1, 1, pal_file);
            fread(&pal[i].g, 1, 1, pal_file);
            fread(&pal[i].b, 1, 1, pal_file);
    }

    fclose(pal_file);
}

void Cel_file::fill_t(size_t pixels, std::vector<colour>& raw_image)
{
    for(int px = 0; px < pixels; px++)
        raw_image.push_back(colour(255, 255, 255, false));
}

int32_t Cel_file::normal_width(uint8_t* frame, size_t frame_size, bool from_header, uint16_t offset)
{
    
    // If we have a header, we know that offset points to the start of the 32nd line.
    // So, when we reach that point, we will have produced 31 lines of pixels, so we 
    // can divide the number of pixels we have passed at this point by 31, to get the 
    // width.
    if(from_header)
    {
        int32_t width_header = 0; 
        
        for(size_t i = 11; i < frame_size; i++){
            
            if(i == offset && from_header)
            {
                width_header = width_header/31;
                break;
            }
            // Regular command
            if(frame[i] <= 127){
                width_header += frame[i];
                i += frame[i];
            }

            // Transparency command
            else if(128 <= frame[i]){
                width_header += 256 - frame[i];
            }
        }

        return width_header;
    }
    
    // If we do not have a header we probably (definitely?) don't have any transparency.
    // The maximum stretch of opaque pixels following a command byte is 127.
    // Since commands can't wrap over lines (it seems), if the width is shorter than 127,
    // the first (command) byte will indicate an entire line, so it's value is the width.
    // If the width is larger than 127, it will be some sequence of 127 byte long stretches,
    // followed by some other value to bring it to the end of a line (presuming the width is
    // not divisible by 127).
    // So, for all image except those whose width is divisible by 127, we can determine width
    // by looping through control bits, adding 127 each time, until we find some value which
    // is not 127, then add that to the 127-total and that is our width.
    else
    {
        int32_t width_reg = 0;
        
        for(size_t i = 0; i < frame_size; i++){

            // Regular command
            if(frame[i] <= 127){
                width_reg += frame[i];
                i += frame[i];
            }

            // Transparency command - who knows, it might be possible
            else if(128 <= frame[i]){
                width_reg += 256 - frame[i];
            }

            if(frame[i] != 127)
                break;

        }

        return width_reg;
    }
}

void Cel_file::normal_decode(uint8_t* frame, size_t frame_size, size_t width, bool from_header, colour* pal, std::vector<colour>& raw_image)
{
    #ifdef CEL_DEBUG
        std::cout << "NORMAL_DECODE" << std::endl;
    #endif

    size_t i;

    // Skip the header if it exists
    if(from_header)
        i = 11;
    else
        i = 0;
    
    for(; i < frame_size; i++)
    {
        // Regular command
        if(frame[i] <= 127)
        {
            #ifdef CEL_DEBUG
                std::cout << i << " regular: " << (int)frame[i] << std::endl;
            #endif
            
            size_t j;
            // Just push the number of pixels specified by the command
            for(j = 1; j < frame[i]+1 && i+j < frame_size; j++)
            {
                int index = i+j;
                uint8_t f = frame[index];
                
                if(index > frame_size-1)
                    std::cout << "invalid read from f " << index << " " << frame_size << std::endl;

                colour col = pal[f];

                raw_image.push_back(col);
            }
            //if(i+j >= frame_size)
            //    std::cout << "----- " << i << " " << j << " " << i+j << " " << frame_size << std::endl;
            
            i+= frame[i];
        }

        // Transparency command
        else if(128 <= frame[i])
        {
            #ifdef CEL_DEBUG
                std::cout << i << " transparency: " << (int)frame[i] << " " << (256 - frame[i]) << std::endl;
            #endif
            
            // Push (256 - command value) transparent pixels
            for(size_t j = 0; j < 256-frame[i]; j++)
                raw_image.push_back(colour(255, 0, 255, false));
        }
    }
}

size_t Cel_file::decode_raw_32(uint8_t* frame, size_t frame_size, colour* pal, std::vector<colour>& raw_image)
{

    for(int i = 0; i < frame_size; i++)
    {
        raw_image.push_back(pal[frame[i]]);
    }

    return 32;
}

bool Cel_file::ends_with(const std::string& full, const std::string& end)
{
    return end.size() <= full.size() && full.substr(full.size() - end.size(), end.size()) == end;
}

std::string Cel_file::replace_end(const std::string& old_end, const std::string& new_end, const std::string& original)
{
    std::string retval = original.substr(0, original.size() - old_end.size());
    retval.append(new_end);
    return retval;
}

bool Cel_file::is_tile_cel(const std::string& file_name)
{
    return 
    ends_with(file_name, "l1.cel") ||
    ends_with(file_name, "l2.cel") ||
    ends_with(file_name, "l3.cel") ||
    ends_with(file_name, "l4.cel") ||
    ends_with(file_name, "town.cel");
}

void Cel_file::get_pallette(std::string filename, colour* pal)
{
    std::string pal_filename;
    if(ends_with(filename, "l1.cel"))
        pal_filename = replace_end("l1.cel", "l1.pal", filename);
    else
        pal_filename = "diablo.pal";

    
    return get_pal(pal_filename, pal);
}