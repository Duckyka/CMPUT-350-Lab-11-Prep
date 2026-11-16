#ifndef TEXTURE_ATLAS_H
#define TEXTURE_ATLAS_H

#include <cassert>
#include <iostream>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vector_util.h"
#include <SFML/Graphics.hpp>

// RectPacker maintains two offsets into an overall bin size:
//  - A line y-offset, representing the current horizontal line on which to place new rectangles
//      horizontally along (the line corresponding to the rectangle tops)
//  - An x-offset into the line, representing the x-coordinate to align the next rectangle left-side
// Furthermore it maintains the maximum texture height encountered along the current line.
// The RectPacker places rectangles left to right along this line until it cannot fit the next
//  rectangle. Once this happens, the line is shifted down to meet the bottom of the tallest texture
//  placed on the line, and the x-offset is reset. This continues until the next rectangle
//  packed cannot be fit on the line within the bin space.
class RectPacker {
public:
    RectPacker(const sf::Vector2f& size)
        : mBinSize(size), mOffset(0.0f, 0.0f), mMaxLineHeight(0.0f) {};

    // pack returns { succeeded, rect id }. If not succeeded, id is irrelevant (but ideally should
    // be set to the maximum possible value).
    std::pair<bool, size_t> pack(const sf::Vector2f& rectSize)
    {
        constexpr size_t InvalidID = std::numeric_limits<size_t>::max();

        // Rect width does not fit in the entire bin or invalid dimensions
        if (rectSize.x <= 0.0f || rectSize.y <= 0.0f || rectSize.x > mBinSize.x)
        {
            return {false, InvalidID};
        }

        // Move to new row if the remaining horizontal space is not enough
        if (mOffset.x + rectSize.x > mBinSize.x)
        {
            mOffset.y += mMaxLineHeight;
            mOffset.x = 0.0f;
            mMaxLineHeight = 0.0f;
        }

        // Rect does not fit vertically in the remaining space
        if (mOffset.y + rectSize.y > mBinSize.y)
        {
            return {false, InvalidID};
        }

        // Rect is good, set rect bounds, get id in Rect vector
        sf::FloatRect currBounds(mOffset, rectSize);
        size_t id = mRects.size();
        mRects.push_back(currBounds);

        // Update marked points for x offset and max rect height in this row
        mOffset.x += rectSize.x;
        mMaxLineHeight = std::max(mMaxLineHeight, rectSize.y);

        return {true, id};
    }

    // Returns the coordinates and size of a previously returned rectangle id.
    // Note that in this coordinate system, y points downwards from the origin at the top-left.
    sf::FloatRect getBounds(size_t rectID) const
    {
        assert(rectID < mRects.size());
        return mRects[rectID];
    }

private:
    sf::Vector2f mBinSize;
    sf::Vector2f mOffset;
    float mMaxLineHeight;
    std::vector<sf::FloatRect> mRects;
};

// Returns a new image which pages the given img with *pixels* pixels on each side.
// The colour of new border pixels are taken by sampling the closest valid pixel in the original
// image.
//
// NOTE: a production implementation might also support other edge wrapping behaviors, like cycling
// around to the other side of the texture. See https://learnopengl.com/Getting-started/Textures
// Our implementation corresponds to GL_CLAMP_TO_EDGE
inline sf::Image padImage(const sf::Image& img, size_t pixels)
{
    sf::Vector2u sourceSize = img.getSize();
    if (sourceSize.x == 0 || sourceSize.y == 0)
    {
        return sf::Image();
    }

    sf::Vector2u destinationSize(sourceSize.x + 2 * pixels, sourceSize.y + 2 * pixels);
    sf::Image result(destinationSize);

    for (unsigned int y = 0; y < destinationSize.y; y++)
    {
        // Clamp y size
        unsigned int sourceY;
        if (y < pixels)
        {
            sourceY = 0;
        }
        else
        {
            sourceY = std::min((unsigned int)(y - pixels), sourceSize.y - 1);
        }

        for (unsigned int x = 0; x < destinationSize.x; x++)
        {
            //Clamp x size
            unsigned int sourceX;
            if (x < pixels)
            {
                sourceX = 0;
            }
            else
            {
                sourceX = std::min((unsigned int)(x - pixels), sourceSize.x - 1);
            }

            result.setPixel({x, y}, img.getPixel({sourceX, sourceY}));
        }
    }
    
    return result;
};

using TextureID = size_t;

class TextureAtlas {
public:
    TextureAtlas(sf::Vector2u binSize, size_t padding)
        : mTexture(binSize), mPacker(sf::Vector2f(mTexture.getSize())), mPadding{padding} {}

    TextureID registerTexture(const std::string& filepath)
    {
        sf::Image image;
        if (!image.loadFromFile(filepath))
        {
            throw std::runtime_error("Texture failed to load: " + filepath);
        }

        sf::Image paddedImage = padImage(image, static_cast<unsigned int>(mPadding));
        auto [result, rectID] = mPacker.pack(sf::Vector2f(paddedImage.getSize()));
        if (!result)
        {
            throw std::runtime_error("The textureAtlas ran out of space for file: " + filepath);
        }

        sf::FloatRect packedBounds = mPacker.getBounds(rectID);
        sf::Vector2u destinationPosition(static_cast<unsigned int>(packedBounds.position.x), static_cast<unsigned int>(packedBounds.position.y));
        mTexture.update(paddedImage, destinationPosition);

        sf::FloatRect innerBounds({packedBounds.position.x + static_cast<float>(mPadding), packedBounds.position.y + static_cast<float>(mPadding)}, sf::Vector2f(image.getSize()));
        TextureID ID = mSubTextures.size();
        mSubTextures.push_back(innerBounds);
        return ID;
    }

    sf::FloatRect getBoundsUV(TextureID id) const
    {
        assert(id < mSubTextures.size());
        const sf::FloatRect& point = mSubTextures[id];
        sf::Vector2f atlasSize(mTexture.getSize());

        return sf::FloatRect({point.position.x / atlasSize.x, point.position.y / atlasSize.y}, {point.size.x / atlasSize.x, point.size.y / atlasSize.y});
    }

    sf::Vector2u getSizePixels(TextureID id) const
    {
        assert(id < mSubTextures.size());
        const sf::FloatRect& point = mSubTextures[id];
        return sf::Vector2u(static_cast<unsigned int>(point.size.x), static_cast<unsigned int>(point.size.y));
    }

    const sf::Texture& getTexture() const { return mTexture; }

private:
    sf::Texture mTexture;
    RectPacker mPacker;
    size_t mPadding;
    std::vector<sf::FloatRect> mSubTextures;
};

#endif  // TEXTURE_ATLAS_H
