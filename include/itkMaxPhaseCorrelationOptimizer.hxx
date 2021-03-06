/*=========================================================================
 *
 *  Copyright Insight Software Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef itkMaxPhaseCorrelationOptimizer_hxx
#define itkMaxPhaseCorrelationOptimizer_hxx

#include "itkMaxPhaseCorrelationOptimizer.h"

#include <cmath>
#include <type_traits>

/*
 * \author Jakub Bican, jakub.bican@matfyz.cz, Department of Image Processing,
 *         Institute of Information Theory and Automation,
 *         Academy of Sciences of the Czech Republic.
 *
 */

namespace itk
{
template < typename TRegistrationMethod >
MaxPhaseCorrelationOptimizer<TRegistrationMethod>
::MaxPhaseCorrelationOptimizer() : Superclass()
{
  m_MaxCalculator = MaxCalculatorType::New();
  m_PeakInterpolationMethod = PeakInterpolationMethod::Parabolic;
  m_ZeroSuppression = 15;
}


template< typename TRegistrationMethod >
void
MaxPhaseCorrelationOptimizer< TRegistrationMethod >
::PrintSelf( std::ostream& os, Indent indent ) const
{
  Superclass::PrintSelf( os, indent );
  os << indent << "MaxCalculator: " << m_MaxCalculator << std::endl;
  auto pim = static_cast< typename std::underlying_type< PeakInterpolationMethod >::type >( m_PeakInterpolationMethod );
  os << indent << "PeakInterpolationMethod: " << pim << std::endl;
}

template< typename TRegistrationMethod >
void
MaxPhaseCorrelationOptimizer< TRegistrationMethod >
::SetPeakInterpolationMethod( const PeakInterpolationMethod peakInterpolationMethod )
{
  if ( this->m_PeakInterpolationMethod != peakInterpolationMethod )
    {
    this->m_PeakInterpolationMethod = peakInterpolationMethod;
    this->Modified();
    }
}

template< typename TRegistrationMethod >
void
MaxPhaseCorrelationOptimizer< TRegistrationMethod >
::ComputeOffset()
{
  ImageConstPointer input = static_cast< ImageType* >( this->GetInput( 0 ) );
  ImageConstPointer fixed = static_cast< ImageType* >( this->GetInput( 1 ) );
  ImageConstPointer moving = static_cast< ImageType* >( this->GetInput( 2 ) );

  OffsetType offset;
  offset.Fill( 0 );

  if ( !input )
    {
    return;
    }

  m_MaxCalculator->SetImage( input );
  m_MaxCalculator->SetN( std::ceil( this->m_Offsets.size() / 2 ) *
                         ( static_cast< unsigned >( std::pow( 3, ImageDimension ) ) - 1 ) );

  try
    {
    m_MaxCalculator->ComputeMaxima();
    }
  catch ( ExceptionObject& err )
    {
    itkDebugMacro( "exception caught during execution of max calculator - passing " );
    throw err;
    }

  typename MaxCalculatorType::ValueVector maxs = m_MaxCalculator->GetMaxima();
  typename MaxCalculatorType::IndexVector indices = m_MaxCalculator->GetIndicesOfMaxima();
  itkAssertOrThrowMacro( maxs.size() == indices.size(),
      "Maxima and their indices must have the same number of elements" );
  std::greater< PixelType > compGreater;
  auto zeroBound = std::upper_bound( maxs.begin(), maxs.end(), 0.0, compGreater );
  if ( zeroBound != maxs.end() ) // there are some non-positive values in here
    {
    unsigned i = zeroBound - maxs.begin();
    maxs.resize( i );
    indices.resize( i );
    }

  // eliminate indices belonging to the same blurry peak
  // condition used is city-block distance of one
  const typename ImageType::RegionType lpr = input->GetLargestPossibleRegion();
  const typename ImageType::SizeType size = lpr.GetSize();
  unsigned i = 1;
  while ( i < indices.size() )
    {
    unsigned k = 0;
    while ( k < i )
      {
      // calculate maximum distance along any dimension
      SizeValueType dist = 0;
      for ( unsigned d = 0; d < ImageDimension; d++ )
        {
        SizeValueType d1 = std::abs( indices[i][d] - indices[k][d] );
        if ( d1 > size[d] / 2 ) // wrap around
          {
          d1 = size[d] - d1;
          }
        dist = std::max( dist, d1 );
        }
      if ( dist < 2 ) // for city-block this is equivalent to:  dist == 1
        {
        break;
        }
      ++k;
      }

    if ( k < i ) // k is nearby
      {
      maxs[k] += maxs[i]; // join amplitudes
      maxs.erase( maxs.begin() + i );
      indices.erase( indices.begin() + i );
      }
    else // examine next index
      {
      ++i;
      }
    }

  // supress trivial zero solution
  const typename ImageType::IndexType oIndex = lpr.GetIndex();
  const PixelType zeroDeemphasis1 = std::max< PixelType >( 1.0, m_ZeroSuppression / 2.0 );
  for ( i = 0; i < maxs.size(); i++ )
    {
    // calculate maximum distance along any dimension
    SizeValueType dist = 0;
    for ( unsigned d = 0; d < ImageDimension; d++ )
      {
      SizeValueType d1 = std::abs( indices[i][d] - oIndex[d] );
      if ( d1 > size[d] / 2 ) // wrap around
        {
        d1 = size[d] - d1;
        }
      dist = std::max( dist, d1 );
      }

    if ( dist == 0 )
      {
      maxs[i] /= m_ZeroSuppression;
      }
    else if ( dist == 1 )
      {
      maxs[i] /= zeroDeemphasis1;
      }
    }

  // now we need to re-sort the values
  {
    std::vector< unsigned > sIndices;
    sIndices.reserve( maxs.size() );
    for ( i = 0; i < maxs.size(); i++ )
      {
      sIndices.push_back( i );
      }
    std::sort( sIndices.begin(), sIndices.end(), [maxs]( unsigned a, unsigned b ) { return maxs[a] > maxs[b]; } );

    // now apply sorted order
    typename MaxCalculatorType::ValueVector tMaxs( maxs.size() );
    typename MaxCalculatorType::IndexVector tIndices( maxs.size() );
    for ( i = 0; i < maxs.size(); i++ )
      {
      tMaxs[i] = maxs[sIndices[i]];
      tIndices[i] = indices[sIndices[i]];
      }
    maxs.swap( tMaxs );
    indices.swap( tIndices );
  }

  if ( this->m_Offsets.size() > maxs.size() )
    {
    this->SetOffsetCount( maxs.size() );
    }
  else
    {
    maxs.resize( this->m_Offsets.size() );
    indices.resize( this->m_Offsets.size() );
    }

  for ( unsigned m = 0; m < maxs.size(); m++ )
    {
    using ContinuousIndexType = ContinuousIndex< OffsetScalarType, ImageDimension >;
    ContinuousIndexType maxIndex = indices[m];

    if ( m_PeakInterpolationMethod != PeakInterpolationMethod::None ) // interpolate the peak
      {
      typename ImageType::PixelType y0, y1 = maxs[m], y2;
      typename ImageType::IndexType tempIndex = indices[m];

      for ( i = 0; i < ImageDimension; i++ )
        {
        tempIndex[i] = maxIndex[i] - 1;
        if ( !lpr.IsInside( tempIndex ) )
          {
          tempIndex[i] = maxIndex[i];
          continue;
          }
        y0 = input->GetPixel( tempIndex );
        tempIndex[i] = maxIndex[i] + 1;
        if ( !lpr.IsInside( tempIndex ) )
          {
          tempIndex[i] = maxIndex[i];
          continue;
          }
        y2 = input->GetPixel( tempIndex );
        tempIndex[i] = maxIndex[i];

        OffsetScalarType omega, theta;
        switch ( m_PeakInterpolationMethod )
          {
          case PeakInterpolationMethod::Parabolic:
            maxIndex[i] += ( y0 - y2 ) / ( 2 * ( y0 - 2 * y1 + y2 ) );
            break;
          case PeakInterpolationMethod::Cosine:
            omega = std::acos( ( y0 + y2 ) / ( 2 * y1 ) );
            theta = std::atan( ( y0 - y2 ) / ( 2 * y1 * std::sin( omega ) ) );
            maxIndex[i] -= ::itk::Math::one_over_pi * theta / omega;
            break;
          default:
            itkAssertInDebugAndIgnoreInReleaseMacro( "Unknown interpolation method" );
            break;
          } // switch PeakInterpolationMethod
        } // for ImageDimension
      } // if Interpolation != None

    const typename ImageType::SpacingType spacing = input->GetSpacing();
    const typename ImageType::PointType   fixedOrigin = fixed->GetOrigin();
    const typename ImageType::PointType   movingOrigin = moving->GetOrigin();

    for ( i = 0; i < ImageDimension; ++i )
      {
      IndexValueType adjustedSize = IndexValueType( size[i] + oIndex[i] );
      OffsetScalarType directOffset = ( movingOrigin[i] - fixedOrigin[i] )
        - 1 * spacing[i] * ( maxIndex[i] - oIndex[i] );
      OffsetScalarType mirrorOffset = ( movingOrigin[i] - fixedOrigin[i] )
        - 1 * spacing[i] * ( maxIndex[i] - adjustedSize );
      if ( std::abs( directOffset ) <= std::abs( mirrorOffset ) )
        {
        offset[i] = directOffset;
        }
      else
        {
        offset[i] = mirrorOffset;
        }
      }

    this->m_Offsets[m] = offset;
    }
}

} // end namespace itk

#endif
