// This code implementation is the intellectual property of
// the RD44 GEANT4 collaboration.
//
// By copying, distributing or modifying the Program (or any work
// based on the Program) you indicate your acceptance of this statement,
// and all its terms.
//
// $Id: G4VDigiCollection.cc,v 2.1 1998/07/12 02:53:40 urbi Exp $
// GEANT4 tag $Name: geant4-00 $
//

// G4VDigiCollection

#include "G4VDigiCollection.hh"

G4VDigiCollection::G4VDigiCollection()
{
  collectionName = "Unknown";
  DMname = "Unknown";
}

G4VDigiCollection::G4VDigiCollection(G4String DMnam,G4String colNam)
{
  collectionName = colNam;
  DMname = DMnam;
}

G4VDigiCollection::~G4VDigiCollection()
{ ; }

int G4VDigiCollection::operator==(const G4VDigiCollection &right) const
{ 
  return ((collectionName==right.collectionName)
        &&(DMname==right.DMname));
}

void G4VDigiCollection::DrawAllDigi()
{;}

void G4VDigiCollection::PrintAllDigi()
{;}
