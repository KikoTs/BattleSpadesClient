/*
 Copyright (c) 2013 OpenSpades Developers
 Modified by Paratrooper 2015
 
 This file is part of OpenSpades.
 
 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 ( at your option ) any later version.
 
 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http:// www.gnu.org/licenses/>.
 
 */
 
namespace spades {
	class ViewRifleSkin: 
				
	IToolSkin, IViewToolSkin, IWeaponSkin,
	BasicViewWeapon {
		private AudioDevice@ audioDevice;
		private Model@ gunModel;
		private Model@ chargingHandleModel;
		private Model@ magazineEmptyModel;
		private Model@ magazineLoadedModel;
		private Model@ sightsFrontModel;
		private Model@ sightsPinModel;
		private Model@ sightsReflexModel;
        ConfigItem cg_fov("cg_fov");
        private ConfigItem cg_fov2("cg_fov2");
        private int fov = cg_fov2.IntValue;
        private ConfigItem cg_zoomFov("cg_zoomFov");
        private ConfigItem cg_runFov("cg_runFov");
		
		private AudioChunk@[] fireSounds( 4 );
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ reloadSound;
		
		private Image@ reflexRound;
		private Image@[] muzzleFlashes( 20 );

		// some important numbers
		private float globalScale = 0.008f;											// scale of the weapon matrix
		private Vector3 weaponPivot = Vector3( 4.5f, 40.f, 17.f );					// pivot of the weapon if viewed from slab6
		private Vector3 sightOffset = Vector3( 4.5f, 98.f, -5.5f ) - weaponPivot;	// the position of the front sight with respect to the pivot of the gun
		private float shootRand1 = GetRandom(); 									// random number every time the gun is fired, used for the random rotation in the Y-axis due to recoil
		private float shootRand2 = GetRandom(); 									// random number every time the gun is fired, used for the random Y-axis translation due to recoil
		private int flashTextureRand = GetRandom( muzzleFlashes.length );			// random number corresponding to the index of the muzzle flash's texture
		private float flashRadiusRand = 0.4f + GetRandom() * 0.1f;					// 
		private float flashRotationRand = GetRandom();

		ViewRifleSkin( Renderer@ r, AudioDevice@ dev ) {
			super( r );
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				( "Models/Weapons/Rifle/WeaponNoMagazine.kv6" );
			@chargingHandleModel = renderer.RegisterModel
				( "Models/Weapons/Rifle/ChargingHandle.kv6" );
			@magazineEmptyModel = renderer.RegisterModel
				( "Models/Weapons/Rifle/MagazineEmpty.kv6" );
			@magazineLoadedModel = renderer.RegisterModel
				( "Models/Weapons/Rifle/MagazineLoaded.kv6" );
			@sightsFrontModel = renderer.RegisterModel
				( "Models/Weapons/Rifle/SightsFront.kv6" );
			
				
			@fireSounds[0] = dev.RegisterSound
				( "Sounds/Weapons/Rifle/FireLocal1.wav" );
			@fireSounds[1] = dev.RegisterSound
				( "Sounds/Weapons/Rifle/FireLocal2.wav" );
			@fireSounds[2] = dev.RegisterSound
				( "Sounds/Weapons/Rifle/FireLocal3.wav" );
			@fireSounds[3] = dev.RegisterSound
				( "Sounds/Weapons/Rifle/FireLocal4.wav" );
			@fireFarSound = dev.RegisterSound
				( "Sounds/Weapons/Rifle/FireFar.wav" );
			@fireStereoSound = dev.RegisterSound
				( "Sounds/Weapons/Rifle/FireStereo.wav" );
			@reloadSound = dev.RegisterSound
				( "Sounds/Weapons/Rifle/ReloadLocal.wav" );
			
			@reflexRound = renderer.RegisterImage
				( "Gfx/ReflexRound.png" );
			
			for ( uint i = 0; i < 20; i++ ) {
				string dir = "Gfx/Flash/Weapons/Rifle/";
				dir += i / 100; 			// hundreds
				dir += i % 100 / 10;		// tens
				dir += i % 10;				// units
				dir += ".png";
				@muzzleFlashes[i] = renderer.RegisterImage( dir );
			}
		}
		
		void Update( float dt ) {
			// dTime = dt;
			BasicViewWeapon::Update( dt );
		}
		
		void WeaponFired() {
			BasicViewWeapon::WeaponFired();

			shootRand1 = Mix( shootRand1, GetRandom(), 0.6f );
			shootRand2 = GetRandom();

			flashTextureRand = GetRandom( muzzleFlashes.length );
			flashRadiusRand = GetRandom();
			flashRotationRand = GetRandom();
			
			if ( !IsMuted ) {
				Vector3 origin = Vector3( 0.4f, -0.3f, 0.5f );
				AudioParam param;
				param.volume = 8.f;
				audioDevice.PlayLocal( fireSounds[GetRandom( fireSounds.length )], origin, param );
				
				param.volume = 4.f;
				audioDevice.PlayLocal( fireFarSound, origin, param );
				param.volume = 1.f;
				audioDevice.PlayLocal( fireStereoSound, origin, param );
			}	
		}
		
		void ReloadingWeapon() {
			if ( !IsMuted ) {
				Vector3 origin = Vector3( 0.4f, -0.3f, 0.5f );
				AudioParam param;
				param.volume = 0.2f;
				audioDevice.PlayLocal( reloadSound, origin, param );
			}
		}
		
		float GetZPos() {
			float zOffset = 0.2f;
			return zOffset + AimDownSightStateSmooth * ( -sightOffset.z * globalScale - zOffset );
		}
		
		float rad( float deg ) {
			return PiF * deg/180.f;
		}
		
		Matrix4 SimpleRotate( Vector3 rot ) {
			Matrix4 rotMatrix = CreateRotateMatrix( Vector3( 1.f, 0.f, 0.f ), rot.x );
			rotMatrix *= CreateRotateMatrix( Vector3( 0.f, 1.f, 0.f ), rot.y );
			rotMatrix *= CreateRotateMatrix( Vector3( 0.f, 0.f, 1.f ), rot.z );
			return rotMatrix;
		}
		
		Matrix4 GetShootRecoilMatrix() {
			Matrix4 mat;
			float per = GetLocalFireVibration(); // jumps to 1 immediately after firing, down to 0 after cooling down
			float peak = 0.8; // peak of the recoil anim
			per /= ( per > peak ) ? ( 1 - peak ) : ( peak );
				
			per = sin( per * PiF * 0.5f ); // ditto, but with some sin easing
			mat *= SimpleRotate( Vector3( 0.f, rad( ( shootRand1 - 0.5f ) * 2.f * 3.f ) , 0.f ) * per );
			mat *= CreateTranslateMatrix( Vector3( 0.f, -1 - shootRand2/2, 0.f ) * 0.01 * per );
			Vector3 tilt = Vector3( rad( -1.f ), 0.f, 0.f );
			
			mat *= SimpleRotate( tilt * per );
			return mat;
		}
		
		Matrix4 GetMovementSwingMatrix() {
			Matrix4 mat;
			mat *= SimpleRotate( Vector3( rad( 10.f ), 0.f, 0.f ) * swing.z/( 0.05f ));
			mat *= CreateTranslateMatrix( Vector3( swing.x, swing.y/2, 0 ));
			mat *= SimpleRotate( Vector3( 0.f, rad( 10.f ) * swing.x/( 0.05f ), 0.f ));
			return mat;
		}
		
		Matrix4 GetViewWeaponMatrixWithRotation( Matrix4 rot ) {
			Matrix4 mat;
			mat *= GetShootRecoilMatrix();
			mat *= GetMovementSwingMatrix();
			if ( sprintState > 0.f ) {
				float per = sprintState;
				per *= per;
				mat *= SimpleRotate( Vector3( 0.f, 0.f, rad( -30.f )) * per );
				mat *= SimpleRotate( Vector3( rad( 10.f ), 0.f, 0.f ) * per );
			}
			
			if ( raiseState < 1.f ) {
				float putdown = 1.f - raiseState;
				putdown = putdown * putdown;
				mat *= SimpleRotate( Vector3( 0.f, 0.f, rad( -30.f )) * putdown );
				mat *= SimpleRotate( Vector3( rad( 80.f ), 0.f, 0.f ) * putdown ); 
			}
			
			Vector3 trans = Vector3( 0.f, 0.f, 0.f );
			trans += Vector3( -0.16f * ( 1 - AimDownSightStateSmooth ) , 0.2f, GetZPos() );
			trans = Mix( trans, trans + Vector3( 0.f, -0.1f, 0.f ), AimDownSightStateSmooth );
			mat *= CreateTranslateMatrix( trans );
			mat *= rot;
			
			return mat;
		}

		
		
		void Draw2D() {
			if(AimDownSightStateSmooth > 0.8){
				Image@ img = renderer.RegisterImage("Gfx/Rifle.png");
				float height = renderer.ScreenHeight;
				float width = height * (1366.f / 768.f); 
				renderer.Color = (Vector4(1.f, 1.f, 1.f, 1.f));
				renderer.DrawImage(img,
					AABB2((renderer.ScreenWidth - width) * 0.5f,
							(renderer.ScreenHeight - height) * 0.5f,
							width, height));
				return;
			}
			BasicViewWeapon::Draw2D();
		}
		
		Vector3 V3QuickMix( Vector3 v0, Vector3 v1, float current, float start, float end ) {
			return Mix( v0, v1, SmoothStep( ( current-start ) / ( end-start ) ) );
		}
		void AddToScene() {
			float zoom = AimDownSightStateSmooth * (cg_zoomFov.IntValue - fov);  //difference in fov (degrees). below zero when aiming
float run = SprintState * (cg_runFov.IntValue - fov); //above zero when sprinting
cg_fov.IntValue = fov + zoom + run;
if (AimDownSightState == 0 && SprintState == 0)
{
   cg_fov.IntValue = fov;
}
			if(AimDownSightStateSmooth > 0.8){
				LeftHandPosition = Vector3(1.f, 8.f, 10.f);
				RightHandPosition = Vector3(0.f, -8.f, 20.f);
				return;
			}
			
			bool reloading = IsReloading;
			float reload = ReloadProgress;
			reload *= 2.5f;
			float aiming = AimDownSightStateSmooth;
			if ( reloading ) {
				aiming *= ( ( reload < 0.5f ) ? ( 1 - ( reload / 0.5f ) ) :
				( reload >= 0.5f && reload < 2.f ) ? ( 0 ) :
				( ( reload - 2.f ) / 0.5f ) ); 
			}
			
			aiming *= aiming;
			
			Vector3 weaponRot = Vector3( 0.f, 0.f, 0.f );
			
			Vector3[] rotTimeline = {weaponRot, // protip: jostle the y-axis and z-axis realistically a bit every frame you get to make it look realistic
				Vector3( rad( -8.f ) , rad( 10.f ), 0.f ), // lhand goes to mag
				Vector3( rad( -24.f ) , rad( 20.f ), 0.f ), // lhand pulls mag out of the well and goes down
				Vector3( rad( -16.f ), rad( 20.f ), 0.f ), // lhand stays down for a bit
				Vector3( rad( -8.f ), rad( 18.f ), 0.f ), // lhand rises up with new mag to the well
				Vector3( rad( -16.f ), rad( 18.f ), 0.f ), // lhand pulls down slightly
				Vector3( rad( -12.f ), rad( 18.f ), 0.f ), // lhand sharply taps mag in place
				Vector3( rad( -16.f ), rad( 14.f ), 0.f ), // lhand keeps hand on mag
				Vector3( rad( -20.f ), rad( 10.f ), 0.f ), // lhand moves to chaging handle
				Vector3( rad( -24.f ), rad( 6.f ), 0.f ), // lhand keeps hand at charging handle for a bit
				Vector3( rad( -28.f ), rad( 8.f ), 0.f ), // lhand pulls charging handle back 
				Vector3( rad( -28.f ), rad( 6.f ), 0.f ), // lhand keeps charging handle back 
				Vector3( rad( -12.f ), rad( 4.f ), 0.f ), // lhand guides charging hande back in place
			weaponRot}; // return to normal
			
			if ( reloading ) {
				if ( reload < 0.1f )
					weaponRot = V3QuickMix( rotTimeline[0], rotTimeline[1], reload, 0.f, 0.1f );
				else if ( reload < 0.4 )
					weaponRot = V3QuickMix( rotTimeline[1], rotTimeline[2], reload, 0.1f, 0.4f );
				else if ( reload < 0.7 )
					weaponRot = V3QuickMix( rotTimeline[2], rotTimeline[3], reload, 0.4f, 0.7f );
				else if ( reload < 1.0 )
					weaponRot = V3QuickMix( rotTimeline[3], rotTimeline[4], reload, 0.7f, 1.0f );
				else if ( reload < 1.2 )
					weaponRot = V3QuickMix( rotTimeline[4], rotTimeline[5], reload, 1.0f, 1.2f );
				else if ( reload < 1.25 )
					weaponRot = V3QuickMix( rotTimeline[5], rotTimeline[6], reload, 1.2f, 1.25f );
				else if ( reload < 1.4 )
					weaponRot = V3QuickMix( rotTimeline[6], rotTimeline[7], reload, 1.25f, 1.4f );
				else if ( reload < 1.7 )
					weaponRot = V3QuickMix( rotTimeline[7], rotTimeline[8], reload, 1.4f, 1.7f );
				else if ( reload < 1.8 )
					weaponRot = V3QuickMix( rotTimeline[8], rotTimeline[9], reload, 1.7f, 1.8f );
				else if ( reload < 2.0 )
					weaponRot = V3QuickMix( rotTimeline[9], rotTimeline[10], reload, 1.8f, 2.0f );
				else if ( reload < 2.1 )
					weaponRot = V3QuickMix( rotTimeline[10], rotTimeline[11], reload, 2.0f, 2.1f );
				else if ( reload < 2.3 )
					weaponRot = V3QuickMix( rotTimeline[11], rotTimeline[12], reload, 2.1f, 2.3f );
				else if ( reload < 2.5 )
					weaponRot = V3QuickMix( rotTimeline[12], rotTimeline[13], reload, 2.3f, 2.5f );
				else // if ( reload >= 2.5 )
					weaponRot = rotTimeline[0];
			}

			Matrix4 rot = SimpleRotate( weaponRot );
			Matrix4 mat = CreateScaleMatrix( globalScale );
			mat = GetViewWeaponMatrixWithRotation( rot ) * mat;
			
            Vector3 leftHand, rightHand;
			leftHand = mat * ( Vector3( 7.f, 94.f, 24.f ) - weaponPivot );
			rightHand = mat * ( Vector3( 4., 34.f, 26.f ) - weaponPivot );

			Vector3[] lhTimeline = {leftHand,
				mat * ( Vector3( 7.5f, 86.f, 57.f ) - weaponPivot ), // lhand goes to mag
				mat * ( Vector3( 7.5f, 60.f, 140.f ) - weaponPivot ), // lhand pulls mag out of the well and goes down
				mat * ( Vector3( 7.5f, 60.f, 140.f ) - weaponPivot ), // lhand stays down for a bit
				mat * ( Vector3( 7.5f, 86.f, 57.f ) - weaponPivot ), // lhand rises up with new mag to the well
				mat * ( Vector3( 5.5f, 82.f, 79.f ) - weaponPivot ), // lhand pulls down slightly
				mat * ( Vector3( 5.5f, 82.f, 61.f ) - weaponPivot ), // lhand sharply taps mag in place
				mat * ( Vector3( 5.5f, 82.f, 61.f ) - weaponPivot ), // lhand keeps hand on mag
				mat * ( Vector3( 18.f, 74.f, 12.f ) - weaponPivot ), // lhand moves to chaging handle
				mat * ( Vector3( 18.f, 74.f, 12.f ) - weaponPivot ), // lhand keeps hand at charging handle for a bit
				mat * ( Vector3( 18.f, 60.f, 12.f ) - weaponPivot ), // lhand pulls charging handle back 
				mat * ( Vector3( 18.f, 60.f, 12.f ) - weaponPivot ), // lhand keeps charging handle back 
				mat * ( Vector3( 18.f, 74.f, 12.f ) - weaponPivot ), // lhand guides charging hande back in place
			leftHand}; // return to normal
			
			if ( reloading ) {
				if ( reload < 0.1f )
					leftHand = V3QuickMix( lhTimeline[0], lhTimeline[1], reload, 0.f, 0.1f );
				else if ( reload < 0.4 )
					leftHand = V3QuickMix( lhTimeline[1], lhTimeline[2], reload, 0.1f, 0.4f );
				else if ( reload < 0.7 )
					leftHand = V3QuickMix( lhTimeline[2], lhTimeline[3], reload, 0.4f, 0.7f );
				else if ( reload < 1.0 )
					leftHand = V3QuickMix( lhTimeline[3], lhTimeline[4], reload, 0.7f, 1.0f );
				else if ( reload < 1.2 )
					leftHand = V3QuickMix( lhTimeline[4], lhTimeline[5], reload, 1.0f, 1.2f );
				else if ( reload < 1.25 )
					leftHand = V3QuickMix( lhTimeline[5], lhTimeline[6], reload, 1.2f, 1.25f );
				else if ( reload < 1.4 )
					leftHand = V3QuickMix( lhTimeline[6], lhTimeline[7], reload, 1.25f, 1.4f );
				else if ( reload < 1.7 )
					leftHand = V3QuickMix( lhTimeline[7], lhTimeline[8], reload, 1.4f, 1.7f );
				else if ( reload < 1.8 )
					leftHand = V3QuickMix( lhTimeline[8], lhTimeline[9], reload, 1.7f, 1.8f );
				else if ( reload < 2.0 )
					leftHand = V3QuickMix( lhTimeline[9], lhTimeline[10], reload, 1.8f, 2.0f );
				else if ( reload < 2.1 )
					leftHand = V3QuickMix( lhTimeline[10], lhTimeline[11], reload, 2.0f, 2.1f );
				else if ( reload < 2.3 )
					leftHand = V3QuickMix( lhTimeline[11], lhTimeline[12], reload, 2.1f, 2.3f );
				else if ( reload < 2.5 )
					leftHand = V3QuickMix( lhTimeline[12], lhTimeline[13], reload, 2.3f, 2.5f );
				else // if ( reload >= 2.5 )
					leftHand = lhTimeline[0];
			}
									
			// Model Rendering
			// draw weapon body
			ModelRenderParam param;
			Matrix4 weapMatrix = eyeMatrix * mat;
			param.matrix = weapMatrix;
			param.depthHack = true;
			renderer.AddModel( gunModel, param );
			
			// draw sights
			// front + barrel
			Matrix4 alignMat = weapMatrix;
			alignMat *= CreateTranslateMatrix( Vector3( 4.5f, 105.f, -4.0f ) - weaponPivot );
			alignMat *= CreateScaleMatrix( 0.25f );
			param.matrix = alignMat;
			renderer.AddModel( sightsFrontModel, param ); 
			
	
			 
			
			// draw charging handle
			alignMat = weapMatrix;
			float charging = 0.f;
			float ready = ReadyState;
			
			// charging sequence
			if ( !reloading ) {
				if ( ready >= 0.8f ) {
					charging = 0.f;
				}
				else if ( ready < 0.3f ) {
					charging = ready * 1.f/0.3f;
					charging = ( ready/0.3f ) * ( ready/0.3f - 2 ) * -1.f; // quad out
				}
				else {// if ( ready < 0.8f )
					charging = 1.f - ( ready - 0.3f ) * 1.f/0.5f; // linear 
				}
			}
			else {
				if ( reload < 1.8f ) {
					charging = 0.f;
				}
				else if ( reload < 2.0f ) {
					charging = ( reload - 1.8f ) / ( 2.0f - 1.8f );
				}
				else if ( reload < 2.1f ) {
					charging = 1.f;
				}
				else if ( reload < 2.3f ) {
					charging = 1.f - ( ( reload - 2.1f ) / ( 2.3f - 2.1f ));
				}
				else 
				{
					charging = 0.f;
				}
			}
			
			alignMat *= CreateTranslateMatrix( Vector3( 5.5f, 67.5f, 2.0f ) - weaponPivot );
			alignMat *= CreateTranslateMatrix( 0.f, -11.9f * charging, -0.0f );
			alignMat *= CreateScaleMatrix( 0.10f );
			param.matrix = alignMat;
			renderer.AddModel( chargingHandleModel, param ); // charging handle
			
			// magazine
			alignMat = weapMatrix;
			alignMat *= CreateTranslateMatrix( Vector3( 4.5f, 59.5f, 14.5f ) - weaponPivot );
			
			// magazine sequence
			float magy = 0.f;	
			float magz = 0.f;	
			if ( reloading ) {
				if ( reload < 0.1f ) {
					magz = 0.f;
					magz = magy;
				}
				else if ( reload < 0.4f ) {
					magy = ( reload - 0.1f ) / ( 0.4f - 0.1f );
					magz = magy;
				}
				else if ( reload < 0.7f ) {
					magy = 1.f;
					magz = magy;
				}
				else if ( reload < 1.f ) {
					magy = 1.f - ( ( reload - 0.7f ) / ( 1.0f - 0.7f ));
					magz = 1.f - ( ( reload - 0.7f ) / ( 1.0f - 0.7f ) * 47.f/50.f );
				}
				else if ( reload < 1.2f ) {
					magy = 0.f;
					magz = 1.f - 47.f/50.f;
				}
				else if ( reload < 1.25 ) {
					magy = 0;
					magz = 1.f - ( 47.f/50.f + ( reload - 1.2f ) / ( 1.25f - 1.2f ) * 3.f/50.f );
				}
				else {
					magy = 0.f;
					magz = 0.f;
				}
			}

			alignMat *= CreateTranslateMatrix( Vector3( -0.2f, -42.f * magy, 50.f * magz )); //Fucking Adjustments!
			alignMat *= CreateScaleMatrix( 0.48f ); // just a bit below 0.5 because it overlaps with the gun model otherwise
			param.matrix = alignMat;
			if ( reloading ) {
				if ( reload < 0.7f )
					renderer.AddModel( magazineEmptyModel, param );
				else if ( reload < 1.f )
					renderer.AddModel( magazineLoadedModel, param );
				else
					renderer.AddModel( magazineEmptyModel, param );
			}
			else
				renderer.AddModel( magazineEmptyModel, param );
			
			// hands
			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
			
			double distance_from_camera = ( weapMatrix * sightOffset - eyeMatrix.GetOrigin() ).Length;
			
			if ( aiming >= 0.8f ) {
				renderer.ColorP = Vector4( 1.f, 0.f, 0.f, 0.f );
				renderer.AddSprite( reflexRound, weapMatrix * sightOffset, 0.1f, 0.f );	
				renderer.ColorP = Vector4( 1.f, 1.f, 1.f, 0.f );
				renderer.AddSprite( reflexRound, weapMatrix * sightOffset, 0.1f, 0.f );	
			}
			
			if ( ready < 0.04 * 9 ) { // muzzle flash appears for 0.04 seconds ( at least 2 frames @ 30 fps or 3 frames @ 60 fps to solve screen tearing
				renderer.ColorP = Vector4( 1.f, 0.7f, 0.4f, 0.f );
				float flashOffset = 120.f;
				ConfigItem r_softSprites("r_softParticles");
				if ( r_softSprites.IntValue > 0 )
					flashOffset = 200.f;
				renderer.AddSprite( muzzleFlashes[flashTextureRand], weapMatrix * ( Vector3( 4.5f, flashOffset, 5.5f ) - weaponPivot ), 0.4f + flashRadiusRand * 0.1f, flashRotationRand * PiF * 2);
			}
		}
	}
	
	IWeaponSkin@ CreateViewRifleSkin( Renderer@ r, AudioDevice@ dev ) {
		return ViewRifleSkin( r, dev );
	}
}
/*
array<float> reloadTimeline = {0.f,
			0.1f, // lhand goes to mag
			0.4f, // lhand pulls mag out of the well and goes down
			0.7f, // lhand stays down for a bit
			1.0f, // lhand rises up with new mag to the well
			1.2f, // lhand pulls down slightly
			1.25f, // lhand sharply taps mag in place
			1.4f, // lhand keeps hand on mag
			1.7f, // lhand moves to chaging handle
			1.8f, // lhand keeps hand at charging handle for a bit
			2.0f, // lhand pulls charging handle back 
			2.1f, // lhand keeps charging handle back 
			2.3f, // lhand guides charging hande back in place
			2.5f}; // return to normal
*/

/* if ( reloading ) {
	for ( uint i = 1; i < reloadTimeline.length; i++ ) {
		if ( reload < reloadTimeline[i] ) {
			float per = ( reload - reloadTimeline[i - 1] ) / ( reloadTimeline[i] - reloadTimeline[i - 1] );
			weaponRot = Mix( rotTimeline[i - 1], rotTimeline[i], SmoothStep( per ));
			break;
		}
	}
} */