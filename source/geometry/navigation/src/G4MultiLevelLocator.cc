//
// ********************************************************************
// * License and Disclaimer                                           *
// *                                                                  *
// * The  Geant4 software  is  copyright of the Copyright Holders  of *
// * the Geant4 Collaboration.  It is provided  under  the terms  and *
// * conditions of the Geant4 Software License,  included in the file *
// * LICENSE and available at  http://cern.ch/geant4/license .  These *
// * include a list of copyright holders.                             *
// *                                                                  *
// * Neither the authors of this software system, nor their employing *
// * institutes,nor the agencies providing financial support for this *
// * work  make  any representation or  warranty, express or implied, *
// * regarding  this  software system or assume any liability for its *
// * use.  Please see the license in the file  LICENSE  and URL above *
// * for the full disclaimer and the limitation of liability.         *
// *                                                                  *
// * This  code  implementation is the result of  the  scientific and *
// * technical work of the GEANT4 collaboration.                      *
// * By using,  copying,  modifying or  distributing the software (or *
// * any work based  on the software)  you  agree  to acknowledge its *
// * use  in  resulting  scientific  publications,  and indicate your *
// * acceptance of all terms of the Geant4 Software license.          *
// ********************************************************************
//
// $Id: G4MultiLevelLocator.cc 90836 2015-06-10 09:31:06Z gcosmo $
//
// Class G4MultiLevelLocator implementation
//
// 27.10.08 - Tatiana Nikitina.
// 04.10.11 - John Apostolakis, revised convergence to use Surface Normal
// ---------------------------------------------------------------------------

#include <iomanip>
#include <exception>

#include "G4ios.hh"
#include "G4MultiLevelLocator.hh"

G4MultiLevelLocator::G4MultiLevelLocator(G4Navigator *theNavigator)
 : G4VIntersectionLocator(theNavigator)
{
  // In case of too slow progress in finding Intersection Point
  // intermediates Points on the Track must be stored.
  // Initialise the array of Pointers [max_depth+1] to do this  
  
  G4ThreeVector zeroV(0.0,0.0,0.0);
  for (G4int idepth=0; idepth<max_depth+1; idepth++ )
  {
    ptrInterMedFT[ idepth ] = new G4FieldTrack( zeroV, zeroV, 0., 0., 0., 0.);
  }
}      

G4MultiLevelLocator::~G4MultiLevelLocator()
{
  for ( G4int idepth=0; idepth<max_depth+1; idepth++)
  {
    delete ptrInterMedFT[idepth];
  }
}

// --------------------------------------------------------------------------
// G4bool G4PropagatorInField::LocateIntersectionPoint( 
//        const G4FieldTrack&       CurveStartPointVelocity,   // A
//        const G4FieldTrack&       CurveEndPointVelocity,     // B
//        const G4ThreeVector&      TrialPoint,                // E
//              G4FieldTrack&       IntersectedOrRecalculated  // Output
//              G4bool&             recalculated )             // Out
// --------------------------------------------------------------------------
//
// Function that returns the intersection of the true path with the surface
// of the current volume (either the external one or the inner one with one
// of the daughters:
//
//     A = Initial point
//     B = another point 
//
// Both A and B are assumed to be on the true path:
//
//     E is the first point of intersection of the chord AB with 
//     a volume other than A  (on the surface of A or of a daughter)
//
// Convention of Use :
//     i) If it returns "true", then IntersectionPointVelocity is set
//       to the approximate intersection point.
//    ii) If it returns "false", no intersection was found.
//          The validity of IntersectedOrRecalculated depends on 'recalculated'
//        a) if latter is false, then IntersectedOrRecalculated is invalid. 
//        b) if latter is true,  then IntersectedOrRecalculated is
//             the new endpoint, due to a re-integration.
// --------------------------------------------------------------------------
// NOTE: implementation taken from G4PropagatorInField
//
G4bool G4MultiLevelLocator::EstimateIntersectionPoint( 
         const  G4FieldTrack&       CurveStartPointVelocity,       // A
         const  G4FieldTrack&       CurveEndPointVelocity,         // B
         const  G4ThreeVector&      TrialPoint,                    // E
                G4FieldTrack&       IntersectedOrRecalculatedFT,   // Output
                G4bool&             recalculatedEndPoint,          // Out
                G4double&           previousSafety,                // In/Out
                G4ThreeVector&      previousSftOrigin)             // In/Out
{
  // Find Intersection Point ( A, B, E )  of true path AB - start at E.

  // Fake exception for test :
  // kill all tracks with charge > 0
  // if ( CurveStartPointVelocity.GetCharge() < 0 ) {
  //   G4cout << "... Testing exception ..." << G4endl;
  //   throw KillTrackException(CurveStartPointVelocity);
  // }

  G4bool found_approximate_intersection = false;
  G4bool there_is_no_intersection       = false;
  
  G4FieldTrack  CurrentA_PointVelocity = CurveStartPointVelocity; 
  G4FieldTrack  CurrentB_PointVelocity = CurveEndPointVelocity;
  G4ThreeVector CurrentE_Point = TrialPoint;
  G4bool        validNormalAtE = false;
  G4ThreeVector NormalAtEntry;

  G4FieldTrack  ApproxIntersecPointV(CurveEndPointVelocity); // FT-Def-Construct
  G4double      NewSafety = 0.0;
  G4bool        last_AF_intersection = false;   

  // G4bool final_section= true;  // Shows whether current section is last
                                  // (i.e. B=full end)
  G4bool first_section = true;
  recalculatedEndPoint = false; 

  G4bool restoredFullEndpoint = false;

  G4int substep_no = 0;

  G4int oldprc;   // cout/cerr precision settings

  // Limits for substep number
  //
  const G4int max_substeps=   10000;  // Test 120  (old value 100 )
  const G4int warn_substeps=   1000;  //      100  

  // Statistics for substeps
  //
  static G4ThreadLocal G4int max_no_seen= -1; 

  //--------------------------------------------------------------------------  
  //  Algorithm for the case if progress in founding intersection is too slow.
  //  Process is defined too slow if after N=param_substeps advances on the
  //  path, it will be only 'fraction_done' of the total length.
  //  In this case the remaining length is divided in two half and 
  //  the loop is restarted for each half.  
  //  If progress is still too slow, the division in two halfs continue
  //  until 'max_depth'.
  //--------------------------------------------------------------------------

  const G4int param_substeps=5;  // Test value for the maximum number
                                 // of substeps
  const G4double fraction_done=0.3;

  G4bool Second_half = false;    // First half or second half of divided step

  // We need to know this for the 'final_section':
  // real 'final_section' or first half 'final_section'
  // In algorithm it is considered that the 'Second_half' is true
  // and it becomes false only if we are in the first-half of level
  // depthness or if we are in the first section

  G4int depth=0; // Depth counts how many subdivisions of initial step made

#ifdef G4DEBUG_FIELD
  static const G4double tolerance = 1.0e-8 * CLHEP::mm; 
  G4ThreeVector  StartPosition= CurveStartPointVelocity.GetPosition(); 
  if( (TrialPoint - StartPosition).mag() < tolerance) 
  {
     G4Exception("G4MultiLevelLocator::EstimateIntersectionPoint()", 
                 "GeomNav1002", JustWarning,
                 "Intersection point F is exactly at start point A." ); 
  }
#endif

  NormalAtEntry = GetSurfaceNormal(CurrentE_Point, validNormalAtE); 

  // Intermediates Points on the Track = Subdivided Points must be stored.
  // Give the initial values to 'InterMedFt'
  // Important is 'ptrInterMedFT[0]', it saves the 'EndCurvePoint'
  //
  *ptrInterMedFT[0] = CurveEndPointVelocity;
  for (G4int idepth=1; idepth<max_depth+1; idepth++ )
  {
    *ptrInterMedFT[idepth]=CurveStartPointVelocity;
  }

  // Final_section boolean store
  //
  G4bool fin_section_depth[max_depth];
  for (G4int idepth=0; idepth<max_depth; idepth++ )
  {
    fin_section_depth[idepth]=true;
  }
  // 'SubStartPoint' is needed to calculate the length of the divided step
  //
  G4FieldTrack SubStart_PointVelocity = CurveStartPointVelocity;
   
  do
  {
    G4int substep_no_p = 0;
    G4bool sub_final_section = false; // the same as final_section,
                                      // but for 'sub_section'
    SubStart_PointVelocity = CurrentA_PointVelocity; 
    do // REPEAT param
    {
      G4ThreeVector Point_A = CurrentA_PointVelocity.GetPosition();  
      G4ThreeVector Point_B = CurrentB_PointVelocity.GetPosition();
       
      // F = a point on true AB path close to point E 
      // (the closest if possible)
      //
      ApproxIntersecPointV = GetChordFinderFor()
                             ->ApproxCurvePointV( CurrentA_PointVelocity, 
                                                  CurrentB_PointVelocity, 
                                                  CurrentE_Point,
                                                  GetEpsilonStepFor());
        // The above method is the key & most intuitive part ...
     
#ifdef G4DEBUG_FIELD
      if( ApproxIntersecPointV.GetCurveLength() > 
          CurrentB_PointVelocity.GetCurveLength() * (1.0 + tolerance) )
      {
        G4Exception("G4multiLevelLocator::EstimateIntersectionPoint()", 
                    "GeomNav0003", FatalException,
                    "Intermediate F point is past end B point" ); 
      }
#endif

      G4ThreeVector CurrentF_Point= ApproxIntersecPointV.GetPosition();

      // First check whether EF is small - then F is a good approx. point 
      // Calculate the length and direction of the chord AF
      //
      G4ThreeVector  ChordEF_Vector = CurrentF_Point - CurrentE_Point;

      G4ThreeVector  NewMomentumDir= ApproxIntersecPointV.GetMomentumDir(); 
      G4double       MomDir_dot_Norm= NewMomentumDir.dot( NormalAtEntry ) ;
      
#ifdef G4DEBUG_FIELD
      if( fVerboseLevel > 3 )
      { 
         G4ThreeVector  ChordAB           = Point_B - Point_A;
         G4double       ABchord_length    = ChordAB.mag(); 
         G4double       MomDir_dot_ABchord;
         MomDir_dot_ABchord = (1.0 / ABchord_length)
                            * NewMomentumDir.dot( ChordAB );
         G4VIntersectionLocator::ReportTrialStep( substep_no, ChordAB,
              ChordEF_Vector, NewMomentumDir, NormalAtEntry, validNormalAtE ); 
         G4cout << " dot( MomentumDir, ABchord_unit ) = "
                << MomDir_dot_ABchord << G4endl;
      }
#endif
      G4bool adequate_angle =
             ( MomDir_dot_Norm >= 0.0 ) // Can use ( > -epsilon) instead
          || (! validNormalAtE );       // Invalid, cannot use this criterion
      G4double EF_dist2 = ChordEF_Vector.mag2();
      if ( ( EF_dist2 <= sqr(fiDeltaIntersection) && ( adequate_angle ) )
        || ( EF_dist2 <= kCarTolerance*kCarTolerance ) )
      { 
        found_approximate_intersection = true;

        // Create the "point" return value
        //
        IntersectedOrRecalculatedFT = ApproxIntersecPointV;
        IntersectedOrRecalculatedFT.SetPosition( CurrentE_Point );

        if ( GetAdjustementOfFoundIntersection() )
        {
          // Try to Get Correction of IntersectionPoint using SurfaceNormal()
          //  
          G4ThreeVector IP;
          G4ThreeVector MomentumDir=ApproxIntersecPointV.GetMomentumDirection();
          G4bool goodCorrection = AdjustmentOfFoundIntersection(Point_A,
                                    CurrentE_Point, CurrentF_Point, MomentumDir,
                                    last_AF_intersection, IP, NewSafety,
                                    previousSafety, previousSftOrigin );
          if ( goodCorrection )
          {
            IntersectedOrRecalculatedFT = ApproxIntersecPointV;
            IntersectedOrRecalculatedFT.SetPosition(IP);
          }
        }
        // Note: in order to return a point on the boundary, 
        //       we must return E. But it is F on the curve.
        //       So we must "cheat": we are using the position at point E
        //       and the velocity at point F !!!
        //
        // This must limit the length we can allow for displacement!
      }
      else  // E is NOT close enough to the curve (ie point F)
      {
        // Check whether any volumes are encountered by the chord AF
        // ---------------------------------------------------------
        // First relocate to restore any Voxel etc information
        // in the Navigator before calling ComputeStep()
        //
        GetNavigatorFor()->LocateGlobalPointWithinVolume( Point_A );

        G4ThreeVector PointG;   // Candidate intersection point
        G4double stepLengthAF; 
        G4bool Intersects_AF = IntersectChord( Point_A,   CurrentF_Point,
                                               NewSafety, previousSafety,
                                               previousSftOrigin,
                                               stepLengthAF,
                                               PointG );
        last_AF_intersection = Intersects_AF;
        if( Intersects_AF )
        {
          // G is our new Candidate for the intersection point.
          // It replaces  "E" and we will repeat the test to see if
          // it is a good enough approximate point for us.
          //       B    <- F
          //       E    <- G
          //
          CurrentB_PointVelocity = ApproxIntersecPointV;
          CurrentE_Point = PointG;  

          G4bool validNormalLast; 
          NormalAtEntry  = GetSurfaceNormal( PointG, validNormalLast ); 
          validNormalAtE = validNormalLast; 

          // By moving point B, must take care if current
          // AF has no intersection to try current FB!!
          //
          fin_section_depth[depth]=false;
          
          
#ifdef G4VERBOSE
          if( fVerboseLevel > 3 )
          {
            G4cout << "G4PiF::LI> Investigating intermediate point"
                   << " at s=" << ApproxIntersecPointV.GetCurveLength()
                   << " on way to full s="
                   << CurveEndPointVelocity.GetCurveLength() << G4endl;
          }
#endif
        }
        else  // not Intersects_AF
        {  
          // In this case:
          // There is NO intersection of AF with a volume boundary.
          // We must continue the search in the segment FB!
          //
          GetNavigatorFor()->LocateGlobalPointWithinVolume( CurrentF_Point );

          G4double stepLengthFB;
          G4ThreeVector PointH;

          // Check whether any volumes are encountered by the chord FB
          // ---------------------------------------------------------

          G4bool Intersects_FB = IntersectChord( CurrentF_Point, Point_B, 
                                                 NewSafety, previousSafety,
                                                 previousSftOrigin,
                                                 stepLengthFB,
                                                 PointH );
          if( Intersects_FB )
          {
            // There is an intersection of FB with a volume boundary
            // H <- First Intersection of Chord FB 

            // H is our new Candidate for the intersection point.
            // It replaces  "E" and we will repeat the test to see if
            // it is a good enough approximate point for us.

            // Note that F must be in volume volA  (the same as A)
            // (otherwise AF would meet a volume boundary!)
            //   A    <- F 
            //   E    <- H
            //
            CurrentA_PointVelocity = ApproxIntersecPointV;
            CurrentE_Point = PointH;

            G4bool validNormalH;
            NormalAtEntry  = GetSurfaceNormal( PointH, validNormalH ); 
            validNormalAtE = validNormalH; 

          }
          else  // not Intersects_FB
          {
            // There is NO intersection of FB with a volume boundary

            if(fin_section_depth[depth])
            {
              // If B is the original endpoint, this means that whatever
              // volume(s) intersected the original chord, none touch the
              // smaller chords we have used.
              // The value of 'IntersectedOrRecalculatedFT' returned is
              // likely not valid 

              // Check on real final_section or SubEndSection
              //
              if( ((Second_half)&&(depth==0)) || (first_section) )
              {
                there_is_no_intersection = true;   // real final_section
              }
              else
              {
                // end of subsection, not real final section 
                // exit from the and go to the depth-1 level 

                substep_no_p = param_substeps+2;  // exit from the loop

                // but 'Second_half' is still true because we need to find
                // the 'CurrentE_point' for the next loop
                //
                Second_half = true; 
                sub_final_section = true;
                
              }
            }
            else
            {
              if(depth==0)
              {
                // We must restore the original endpoint
                //
                CurrentA_PointVelocity = CurrentB_PointVelocity;  // Got to B
                CurrentB_PointVelocity = CurveEndPointVelocity;
                SubStart_PointVelocity = CurrentA_PointVelocity;
                restoredFullEndpoint = true;
              }
              else
              {
                // We must restore the depth endpoint
                //
                CurrentA_PointVelocity = CurrentB_PointVelocity;  // Got to B
                CurrentB_PointVelocity =  *ptrInterMedFT[depth];
                SubStart_PointVelocity = CurrentA_PointVelocity;
                restoredFullEndpoint = true;
              }
            }
          } // Endif (Intersects_FB)
        } // Endif (Intersects_AF)

        // Ensure that the new endpoints are not further apart in space
        // than on the curve due to different errors in the integration
        //
        G4double linDistSq, curveDist; 
        linDistSq = ( CurrentB_PointVelocity.GetPosition() 
                    - CurrentA_PointVelocity.GetPosition() ).mag2(); 
        curveDist = CurrentB_PointVelocity.GetCurveLength()
                    - CurrentA_PointVelocity.GetCurveLength();

        // Change this condition for very strict parameters of propagation 
        //
        if(    (curveDist>= 0.0) 
            && (curveDist*curveDist*(1+2* GetEpsilonStepFor()) < linDistSq ) )
        {  
          G4FieldTrack oldPointVelB = CurrentB_PointVelocity; 
          G4FieldTrack newEndPointFT= CurrentB_PointVelocity;  // Unused

          if (curveDist>0.0) 
          {
             // Re-integrate to obtain a new B
             newEndPointFT=
                  ReEstimateEndpoint( CurrentA_PointVelocity,
                                      CurrentB_PointVelocity,
                                      linDistSq,    // to avoid recalculation
                                      curveDist );
          }else{
             // Zero length -> no advance!
             newEndPointFT= CurrentA_PointVelocity;
             G4cerr << "G4MultiLevel : WARNING > " 
                    << " Unexpected overlap in curve-distance of A & B "
                    << "  - after Endif (Intersects_AF)."      << G4endl
                    << " This creates a coincidence of A & B points. "
                    << G4endl;
          }
          CurrentB_PointVelocity = newEndPointFT;
           
          if ( (fin_section_depth[depth])           // real final section
             &&( first_section || ((Second_half)&&(depth==0)) ) )
          {
            recalculatedEndPoint = true;
            IntersectedOrRecalculatedFT = newEndPointFT;
              // So that we can return it, if it is the endpoint!
          }
        }

        if( curveDist < 0.0 )
        {
          G4int currentVerboseLevel = fVerboseLevel;
          fVerboseLevel = 5; // Print out a maximum of information
          printStatus( CurrentA_PointVelocity,  CurrentB_PointVelocity,
                       -1.0, NewSafety,  substep_no );
          std::ostringstream message;
          message << "Error in advancing propagation." << G4endl
                  << "        Point A (start) is " << CurrentA_PointVelocity
                  << G4endl
                  << "        Point B (end)   is " << CurrentB_PointVelocity
                  << G4endl
                  << "        Curve distance is " << curveDist << G4endl
                  << G4endl
                  << "The final curve point is not further along"
                  << " than the original!" << G4endl;
          message << " Value of fEpsStep= " << GetEpsilonStepFor() << G4endl;
          
          oldprc = G4cerr.precision(20);
          message << " Point A (Curve start)   is " << CurveStartPointVelocity
                  << G4endl
                  << " Point B (Curve   end)   is " << CurveEndPointVelocity
                  << G4endl
                  << " Point A (Current start) is " << CurrentA_PointVelocity
                  << G4endl
                  << " Point B (Current end)   is " << CurrentB_PointVelocity
                  << G4endl
                  << " Point S (Sub start)     is " << SubStart_PointVelocity
                  << G4endl
                  << " Point E (Trial Point)   is " << CurrentE_Point
                  << G4endl
                  << " Point F (Intersection)  is " << ApproxIntersecPointV
                  << G4endl
                  << "        LocateIntersection parameters are : Substep no= "
                  << substep_no << G4endl
                  << "        Substep depth no= "<< substep_no_p  << " Depth= "
                  << depth;
          G4cerr.precision(oldprc);

          //G4Exception("G4MultiLevelLocator::EstimateIntersectionPoint()",
          //            "GeomNav0003 ", FatalException, message);
          G4Exception("G4MultiLevelLocator::EstimateIntersectionPoint(1)",
                      "GeomNav0003 (Fatal->Warning) ", JustWarning, message);
          fVerboseLevel = currentVerboseLevel;
          // Do not throw but mark that we get here
          throw KillTrackException(CurveStartPointVelocity);
        }
        if(restoredFullEndpoint)
        {
          fin_section_depth[depth] = restoredFullEndpoint;
          restoredFullEndpoint = false;
        }
      } // EndIf ( E is close enough to the curve, ie point F. )
        // tests ChordAF_Vector.mag() <= maximum_lateral_displacement 

#ifdef G4DEBUG_LOCATE_INTERSECTION  
      static G4int trigger_substepno_print= warn_substeps - 20 ;

      if( substep_no >= trigger_substepno_print )
      {
        G4cout << "Difficulty in converging in "
               << "G4MultiLevelLocator::EstimateIntersectionPoint():"
               << G4endl
               << "    Substep no = " << substep_no << G4endl;
        if( substep_no == trigger_substepno_print )
        {
          printStatus( CurveStartPointVelocity, CurveEndPointVelocity,
                       -1.0, NewSafety, 0);
        }
        G4cout << " State of point A: "; 
        printStatus( CurrentA_PointVelocity, CurrentA_PointVelocity,
                     -1.0, NewSafety, substep_no-1, 0);
        G4cout << " State of point B: "; 
        printStatus( CurrentA_PointVelocity, CurrentB_PointVelocity,
                     -1.0, NewSafety, substep_no);
      }
#endif
      substep_no++; 
      substep_no_p++;

    } while (  ( ! found_approximate_intersection )
            && ( ! there_is_no_intersection )     
            && ( substep_no_p <= param_substeps) );  // UNTIL found or
                                                     // failed param substep
    first_section = false;

    if( (!found_approximate_intersection) && (!there_is_no_intersection) )
    {
      G4double did_len = std::abs( CurrentA_PointVelocity.GetCurveLength()
                       - SubStart_PointVelocity.GetCurveLength()); 
      G4double all_len = std::abs( CurrentB_PointVelocity.GetCurveLength()
                       - SubStart_PointVelocity.GetCurveLength());
   
      G4double stepLengthAB;
      G4ThreeVector PointGe;
      // Check if progress is too slow and if it possible to go deeper,
      // then halve the step if so
      //
      if( ( ( did_len )<fraction_done*all_len)
         && (depth<max_depth) && (!sub_final_section) )
      {
        Second_half=false;
        depth++;

        G4double Sub_len = (all_len-did_len)/(2.);
        G4FieldTrack start = CurrentA_PointVelocity;
        G4MagInt_Driver* integrDriver
                         = GetChordFinderFor()->GetIntegrationDriver();
        integrDriver->AccurateAdvance(start, Sub_len, GetEpsilonStepFor());
        *ptrInterMedFT[depth] = start;
        CurrentB_PointVelocity = *ptrInterMedFT[depth];
 
        // Adjust 'SubStartPoint' to calculate the 'did_length' in next loop
        //
        SubStart_PointVelocity = CurrentA_PointVelocity;

        // Find new trial intersection point needed at start of the loop
        //
        G4ThreeVector Point_A = CurrentA_PointVelocity.GetPosition();
        G4ThreeVector SubE_point = CurrentB_PointVelocity.GetPosition();   
     
        GetNavigatorFor()->LocateGlobalPointWithinVolume(Point_A);
        G4bool Intersects_AB = IntersectChord(Point_A, SubE_point,
                                              NewSafety, previousSafety,
                                              previousSftOrigin, stepLengthAB,
                                              PointGe);
        if( Intersects_AB )
        {
          last_AF_intersection = Intersects_AB;
          CurrentE_Point = PointGe;
          fin_section_depth[depth]=true;

          G4bool validNormalAB; 
          NormalAtEntry  = GetSurfaceNormal( PointGe, validNormalAB ); 
          validNormalAtE = validNormalAB;
        }
        else
        {
          // No intersection found for first part of curve
          // (CurrentA,InterMedPoint[depth]). Go to the second part
          //
          Second_half = true;
        }
      } // if did_len

      if( (Second_half)&&(depth!=0) )
      {
        // Second part of curve (InterMed[depth],Intermed[depth-1])) 
        // On the depth-1 level normally we are on the 'second_half'

        Second_half = true;
        //  Find new trial intersection point needed at start of the loop
        //
        SubStart_PointVelocity = *ptrInterMedFT[depth];
        CurrentA_PointVelocity = *ptrInterMedFT[depth];
        CurrentB_PointVelocity = *ptrInterMedFT[depth-1];
         // Ensure that the new endpoints are not further apart in space
        // than on the curve due to different errors in the integration
        //
        G4double linDistSq, curveDist; 
        linDistSq = ( CurrentB_PointVelocity.GetPosition() 
                    - CurrentA_PointVelocity.GetPosition() ).mag2(); 
        curveDist = CurrentB_PointVelocity.GetCurveLength()
                    - CurrentA_PointVelocity.GetCurveLength();
        if( (curveDist>0.0) && (curveDist*curveDist*(1+2*GetEpsilonStepFor() ) < linDistSq ) )
        {
          // Re-integrate to obtain a new B
          //
          G4FieldTrack newEndPointFT=
                  ReEstimateEndpoint( CurrentA_PointVelocity,
                                      CurrentB_PointVelocity,
                                      linDistSq,    // to avoid recalculation
                                      curveDist );
          G4FieldTrack oldPointVelB = CurrentB_PointVelocity; 
          CurrentB_PointVelocity = newEndPointFT;
          if (depth==1)
          {
            recalculatedEndPoint = true;
            IntersectedOrRecalculatedFT = newEndPointFT;
            // So that we can return it, if it is the endpoint!
          }
        }

        G4ThreeVector Point_A    = CurrentA_PointVelocity.GetPosition();
        G4ThreeVector SubE_point = CurrentB_PointVelocity.GetPosition();   
        GetNavigatorFor()->LocateGlobalPointWithinVolume(Point_A);
        G4bool Intersects_AB = IntersectChord(Point_A, SubE_point, NewSafety,
                                              previousSafety,
                                              previousSftOrigin, stepLengthAB,
                                              PointGe);
        if( Intersects_AB )
        {
          last_AF_intersection = Intersects_AB;
          CurrentE_Point = PointGe;

          G4bool validNormalAB; 
          NormalAtEntry  = GetSurfaceNormal( PointGe, validNormalAB ); 
          validNormalAtE = validNormalAB;
        }       
        depth--;
        fin_section_depth[depth]=true;
      }
    }  // if(!found_aproximate_intersection)

  } while ( ( ! found_approximate_intersection )
            && ( ! there_is_no_intersection )     
            && ( substep_no <= max_substeps) ); // UNTIL found or failed

  if( substep_no > max_no_seen )
  {
    max_no_seen = substep_no; 
#ifdef G4DEBUG_LOCATE_INTERSECTION  
    if( max_no_seen > warn_substeps )
    {
      trigger_substepno_print = max_no_seen-20; // Want to see last 20 steps 
    } 
#endif
  }

  if(  ( substep_no >= max_substeps)
      && !there_is_no_intersection
      && !found_approximate_intersection )
  {
    G4cout << "ERROR - G4MultiLevelLocator::EstimateIntersectionPoint()"
           << G4endl
           << "        Start and end-point of requested Step:" << G4endl;
    printStatus( CurveStartPointVelocity, CurveEndPointVelocity,
                 -1.0, NewSafety, 0);
    G4cout << "        Start and end-point of current Sub-Step:" << G4endl;
    printStatus( CurrentA_PointVelocity, CurrentA_PointVelocity,
                 -1.0, NewSafety, substep_no-1);
    printStatus( CurrentA_PointVelocity, CurrentB_PointVelocity,
                 -1.0, NewSafety, substep_no);
    std::ostringstream message;
    message << "Too many substeps!" << G4endl
            << "         Convergence is requiring too many substeps: "
            << substep_no << G4endl
            << "          Abandoning effort to intersect. " << G4endl
            << "          Found intersection = "
            << found_approximate_intersection << G4endl
            << "          Intersection exists = "
            << !there_is_no_intersection << G4endl;
 
#ifdef FUTURE_CORRECTION
    // Attempt to correct the results of the method // FIX - TODO

    if ( ! found_approximate_intersection )
    { 
      recalculatedEndPoint = true;
      // Return the further valid intersection point -- potentially A ??
      // JA/19 Jan 2006
      IntersectedOrRecalculatedFT = CurrentA_PointVelocity;

      message << "          Did not convergence after " << substep_no
              << " substeps." << G4endl
              << "          The endpoint was adjused to pointA resulting"
              << G4endl
              << "          from the last substep: " << CurrentA_PointVelocity
              << G4endl;
    }
#endif

    oldprc = G4cout.precision( 10 ); 
    G4double done_len = CurrentA_PointVelocity.GetCurveLength(); 
    G4double full_len = CurveEndPointVelocity.GetCurveLength();
    message << "        Undertaken only length: " << done_len
            << " out of " << full_len << " required." << G4endl
            << "        Remaining length = " << full_len - done_len; 
    G4cout.precision( oldprc ); 

    //G4Exception("G4MultiLevelLocator::EstimateIntersectionPoint()",
    //            "GeomNav0003", FatalException, message);
    G4Exception("G4MultiLevelLocator::EstimateIntersectionPoint(2)",
                "GeomNav0003 (Fatal->Warning)", JustWarning, message);
    throw KillTrackException(CurveStartPointVelocity);
  }
  else if( substep_no >= warn_substeps )
  {  
    oldprc = G4cout.precision( 10 ); 
    std::ostringstream message;
    message << "Many substeps while trying to locate intersection."
            << G4endl
            << "          Undertaken length: "  
            << CurrentB_PointVelocity.GetCurveLength()
            << " - Needed: "  << substep_no << " substeps." << G4endl
            << "          Warning level = " << warn_substeps
            << " and maximum substeps = " << max_substeps;
    G4Exception("G4MultiLevelLocator::EstimateIntersectionPoint()",
                "GeomNav1002", JustWarning, message);
    G4cout.precision( oldprc ); 
  }
  return  !there_is_no_intersection; //  Success or failure
}

G4MultiLevelLocator::KillTrackException::KillTrackException(const  G4FieldTrack& fieldTrack)
{
  G4int oldprc = G4cout.precision( 10 ); 
  std::ostringstream message;
  message << " Cannot continue propagating track, track will be killed !!!" << G4endl
          << "   particle momentum [Mev]: " << fieldTrack.GetMomentum() / CLHEP::MeV << G4endl
          << "   particle kinetic energy [MeV]: " << fieldTrack.GetKineticEnergy() / CLHEP::MeV << G4endl
          << "   particle charge: " << fieldTrack.GetCharge() << G4endl
          << "   particle rest mass [GeV]: " << fieldTrack.GetRestMass() / CLHEP::GeV << G4endl;
  G4Exception("G4MultiLevelLocator::G4MultiLevelLocator::KillTrackException",
              "GeomNav1002", JustWarning, message);
  G4cout.precision( oldprc ); 
}

