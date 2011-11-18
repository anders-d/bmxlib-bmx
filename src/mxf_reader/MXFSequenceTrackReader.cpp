/*
 * Copyright (C) 2011  British Broadcasting Corporation.
 * All Rights Reserved.
 *
 * Author: Philip de Nier
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <im/mxf_reader/MXFSequenceTrackReader.h>
#include <im/mxf_reader/MXFSequenceReader.h>
#include <im/IMException.h>
#include <im/Logging.h>

using namespace std;
using namespace im;
using namespace mxfpp;


#define DISABLED_SEG_READ_LIMIT     (-9999)



MXFSequenceTrackReader::MXFSequenceTrackReader(MXFSequenceReader *sequence_reader)
{
    mSequenceReader = sequence_reader;
    mTrackInfo = 0;
    mFileDescriptor = 0;
    mFileSourcePackage = 0;
    mIsEnabled = true;
    mReadStartPosition = 0;
    mReadEndPosition = 0;
    mSampleRate = ZERO_RATIONAL;
    mPosition = 0;
    mDuration = 0;
    mFrameBuffer = new MXFDefaultFrameBuffer();
    mOwnFrameBuffer = true;
}

MXFSequenceTrackReader::~MXFSequenceTrackReader()
{
    delete mTrackInfo;
    if (mOwnFrameBuffer)
        delete mFrameBuffer;
}

bool MXFSequenceTrackReader::IsCompatible(MXFTrackReader *segment) const
{
    if (!segment->IsEnabled())
        return false;

    if (mTrackSegments.empty())
        return true;

    if (!mTrackSegments[0]->GetTrackInfo()->IsCompatible(segment->GetTrackInfo()) ||
        mTrackSegments[0]->GetTrackInfo()->material_track_number != segment->GetTrackInfo()->material_track_number)
    {
        return false;
    }

    return true;
}

void MXFSequenceTrackReader::AppendSegment(MXFTrackReader *segment)
{
    mSegmentOffsets.push_back(GetDuration());

    segment->SetFrameBuffer(mFrameBuffer, false);

    if (mTrackSegments.empty()) {
        mTrackInfo = segment->GetTrackInfo()->Clone();
        mFileDescriptor = segment->GetFileDescriptor();
        mFileSourcePackage = segment->GetFileSourcePackage();
        mSampleRate = segment->GetSampleRate();
        mDuration = segment->GetDuration();
    } else {
        // not valid because multiple segments means there are multiple file source packages
        mFileSourcePackage = 0;

        // set non-constant properties to null
        const MXFTrackInfo *segment_track_info = segment->GetTrackInfo();
        if (segment_track_info->material_package_uid != mTrackInfo->material_package_uid) {
            mTrackInfo->material_package_uid = g_Null_UMID;
            mTrackInfo->material_track_id = 0;
        } else if (segment_track_info->material_track_id != mTrackInfo->material_track_id) {
            mTrackInfo->material_track_id = 0;
        }
        if (segment_track_info->material_track_number != mTrackInfo->material_track_number)
            mTrackInfo->material_track_number = 0;

        if (segment_track_info->file_package_uid != mTrackInfo->file_package_uid) {
            mTrackInfo->file_package_uid = g_Null_UMID;
            mTrackInfo->file_track_id = 0;
        } else if (segment_track_info->file_track_id != mTrackInfo->file_track_id) {
            mTrackInfo->file_track_id = 0;
        }
        if (segment_track_info->file_track_number != mTrackInfo->file_track_number)
            mTrackInfo->file_track_number = 0;

        IM_ASSERT(segment->GetSampleRate() == mTrackInfo->edit_rate);
        mTrackInfo->duration += segment->GetDuration();
        mDuration += segment->GetDuration();
    }

    mTrackSegments.push_back(segment);
}

MXFTrackReader* MXFSequenceTrackReader::GetSegment(size_t index)
{
    IM_CHECK(index < mTrackSegments.size());
    return mTrackSegments[index];
}

void MXFSequenceTrackReader::UpdateReadLimits()
{
    mReadStartPosition = 0;
    mReadEndPosition = 0;

    if (mTrackSegments.empty())
        return;

    // get read start position from first enabled segment
    int64_t read_duration = 0;
    size_t i;
    for (i = 0; i < mTrackSegments.size(); i++) {
        if (mTrackSegments[i]->GetReadStartPosition() > DISABLED_SEG_READ_LIMIT)
            break;
        read_duration += mTrackSegments[i]->GetDuration();
    }
    if (i >= mTrackSegments.size())
        return; // nothing enabled
    mReadStartPosition = read_duration + mTrackSegments[i]->GetReadStartPosition();

    // get read end position from the last enabled segment
    for (; i < mTrackSegments.size(); i++) {
        if (mTrackSegments[i]->GetReadEndPosition() <= DISABLED_SEG_READ_LIMIT)
            break;
        read_duration += mTrackSegments[i]->GetDuration();
    }
    if (i == 0) {
        mReadEndPosition = mTrackSegments[0]->GetReadEndPosition();
    } else {
        mReadEndPosition = read_duration - mTrackSegments[i - 1]->GetDuration() +
                                mTrackSegments[i - 1]->GetReadEndPosition();
    }
}

void MXFSequenceTrackReader::SetEnable(bool enable)
{
    mIsEnabled = enable;

    size_t i;
    for (i = 0; i < mTrackSegments.size(); i++)
        mTrackSegments[i]->SetEnable(enable);
}

void MXFSequenceTrackReader::SetFrameBuffer(MXFFrameBuffer *frame_buffer, bool take_ownership)
{
    if (mOwnFrameBuffer)
        delete mFrameBuffer;

    mFrameBuffer = frame_buffer;
    mOwnFrameBuffer = take_ownership;
}

void MXFSequenceTrackReader::SetReadLimits()
{
    SetReadLimits(GetPrecharge(0, true), mDuration + GetRollout(mDuration - 1, true), true);
}

void MXFSequenceTrackReader::SetReadLimits(int64_t start_position, int64_t end_position, bool seek_to_start)
{
    MXFTrackReader *start_segment, *end_segment;
    int64_t start_segment_position, end_segment_position;
    GetSegmentPosition(start_position, &start_segment, &start_segment_position);
    GetSegmentPosition(end_position, &end_segment, &end_segment_position);

    if (start_segment == end_segment) {
        start_segment->SetReadLimits(start_segment_position, end_segment_position, false);
    } else {
        // end == start_segment->GetDuration() is safe because the start segment has 0 rollout
        start_segment->SetReadLimits(start_segment_position, start_segment->GetDuration(), false);
        // start == 0 is safe because the end segment has 0 pre-charge
        end_segment->SetReadLimits(0, end_segment_position, false);
    }

    // effectively disable segments before the start segment and after the end segment
    size_t i;
    for (i = 0; i < mTrackSegments.size(); i++) {
        if (mTrackSegments[i] == start_segment)
            break;

        mTrackSegments[i]->SetReadLimits(DISABLED_SEG_READ_LIMIT, DISABLED_SEG_READ_LIMIT, false);
    }
    for (; i < mTrackSegments.size(); i++) {
        if (mTrackSegments[i] == end_segment)
            break;
    }
    for (i = i + 1; i < mTrackSegments.size(); i++)
        mTrackSegments[i]->SetReadLimits(DISABLED_SEG_READ_LIMIT, DISABLED_SEG_READ_LIMIT, false);


    UpdateReadLimits();
    mSequenceReader->UpdateReadLimits();


    if (seek_to_start)
        Seek(start_position);
}

uint32_t MXFSequenceTrackReader::Read(uint32_t num_samples, int64_t frame_position_in)
{
    if (!mIsEnabled)
        return 0;

    int64_t frame_position = frame_position_in;
    if (frame_position_in == CURRENT_POSITION_VALUE)
        frame_position = mPosition;

    MXFTrackReader *segment;
    int64_t segment_position;
    GetSegmentPosition(mPosition, &segment, &segment_position);

    uint32_t total_num_read = 0;
    MXFTrackReader *prev_segment;
    do {
        // seek if segment is out of position
        if (segment_position != segment->GetPosition())
            segment->Seek(segment_position);

        uint32_t num_read = segment->Read(num_samples - total_num_read, frame_position);

        // signal that existing frame will be extended if this is not the last read
        if (total_num_read == 0 && num_read < num_samples)
            mFrameBuffer->ExtendFrame(frame_position, true);

        total_num_read += num_read;

        prev_segment = segment;
        GetSegmentPosition(mPosition + total_num_read, &segment, &segment_position);
    }
    while (total_num_read < num_samples && segment != prev_segment);

    mFrameBuffer->ExtendFrame(frame_position, false);


    // always be positioned num_samples after previous position
    mPosition += num_samples;

    return total_num_read;
}

void MXFSequenceTrackReader::Seek(int64_t position)
{
    IM_CHECK(!mTrackSegments.empty());

    MXFTrackReader *segment;
    int64_t segment_position;
    GetSegmentPosition(position, &segment, &segment_position);

    segment->Seek(segment_position);

    mPosition = position;
}

bool MXFSequenceTrackReader::GetIndexEntry(MXFIndexEntryExt *entry, int64_t position) const
{
    IM_CHECK(!mTrackSegments.empty());

    // TODO: need to adjust index entries for the sequence or add source file info to give the entry context
    MXFTrackReader *segment;
    int64_t segment_position;
    GetSegmentPosition(position, &segment, &segment_position);

    return segment->GetIndexEntry(entry, segment_position);
}

int16_t MXFSequenceTrackReader::GetPrecharge(int64_t position, bool limit_to_available) const
{
    IM_CHECK(!mTrackSegments.empty());

    MXFTrackReader *segment;
    int64_t segment_position;
    GetSegmentPosition(position, &segment, &segment_position);

    return segment->GetPrecharge(segment_position, limit_to_available);
}

int16_t MXFSequenceTrackReader::GetRollout(int64_t position, bool limit_to_available) const
{
    IM_CHECK(!mTrackSegments.empty());

    MXFTrackReader *segment;
    int64_t segment_position;
    GetSegmentPosition(position, &segment, &segment_position);

    return segment->GetRollout(segment_position, limit_to_available);
}

SourcePackage* MXFSequenceTrackReader::GetFileSourcePackage() const
{
    IM_CHECK(mFileSourcePackage);
    return mFileSourcePackage;
}

void MXFSequenceTrackReader::Reset(int64_t position)
{
    mFrameBuffer->ResetFrame(position);
}

void MXFSequenceTrackReader::GetSegmentPosition(int64_t position, MXFTrackReader **segment,
                                                int64_t *segment_position) const
{
    IM_CHECK(!mTrackSegments.empty());

    size_t i;
    for (i = 0; i < mSegmentOffsets.size(); i++) {
        if (position < mSegmentOffsets[i])
            break;
    }

    if (i == 0) {
        *segment = mTrackSegments[0];
        *segment_position = position;
    } else {
        *segment = mTrackSegments[i - 1];
        *segment_position = position - mSegmentOffsets[i - 1];
    }
}
