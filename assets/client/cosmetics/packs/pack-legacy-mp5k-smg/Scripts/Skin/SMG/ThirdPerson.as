/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

 namespace spades {
	class ThirdPersonSMGSkin:
	IToolSkin, IThirdPersonToolSkin, IWeaponSkin, IWeaponSkin2 {
		private float sprintState;
		private float raiseState;
		private Vector3 teamColor;
		private bool muted;
		private Matrix4 originMatrix;
		private float aimDownSightState;
		private float readyState;
		private bool reloading;
		private float reloadProgress;
		private int ammo, clipSize;

		private float environmentRoom;
		private float environmentSize;
		private float environmentDistance;
		private Vector3 soundOrigin;

		float SprintState {
			set { sprintState = value; }
		}

		float RaiseState {
			set { raiseState = value; }
		}

		Vector3 TeamColor {
			set { teamColor = value; }
		}

		bool IsMuted {
			set { muted = value; }
		}

		Matrix4 OriginMatrix {
			set { originMatrix = value; }
		}

		float PitchBias {
			get { return 0.f; }
		}

		float AimDownSightState {
			set { aimDownSightState = value; }
		}

		bool IsReloading {
			set { reloading = value; }
		}
		float ReloadProgress {
			set { reloadProgress = value; }
		}
		int Ammo {
			set { ammo = value; }
		}
		int ClipSize {
			set { clipSize = value; }
		}

		float ReadyState {
			set { readyState = value; }
		}

		// IWeaponSkin2
		void SetSoundEnvironment(float room, float size, float distance) {
			environmentRoom = room;
			environmentSize = size;
			environmentDistance = distance;
		}
		Vector3 SoundOrigin {
			set { soundOrigin = value; }
		}

		private Renderer@ renderer;
		private AudioDevice@ audioDevice;

		private AudioChunk@[] spfireMediumSounds(4);
		private AudioChunk@[] fireMediumSounds(4);
		private AudioChunk@ spfireFarSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@[] fireSmallReverbSounds(4);
		private AudioChunk@[] fireLargeReverbSounds(4);
		private AudioChunk@ reloadSound;
		
		private Model@ Weapon;
		private Model@ chargingHandle;
		private Model@ FrontSight;
		private Model@ Suppressor;
		private Model@ ironSight;
		private Model@ MagFull;
		private ConfigItem mp5_sp("mp5_sp", "1");

		ThirdPersonSMGSkin(Renderer@ r, AudioDevice@ dev) {
			@renderer = r;
			@audioDevice = dev;
			
			@Weapon = renderer.RegisterModel("Models/Weapons/SMG/WeaponFP.kv6");
			@MagFull = renderer.RegisterModel("Models/Weapons/SMG/MagazineFull.kv6");
			@chargingHandle = renderer.RegisterModel("Models/Weapons/SMG/ChargingHandle.kv6");
			@FrontSight = renderer.RegisterModel("Models/Weapons/SMG/FrontSight.kv6");
			@Suppressor = renderer.RegisterModel("Models/Weapons/SMG/Suppressor.kv6");
			@ironSight = renderer.RegisterModel("Models/Weapons/SMG/ironSight.kv6");

			@spfireMediumSounds[0] = dev.RegisterSound
				("Sounds/Weapons/SMG/spV2Third1.wav");
			@spfireMediumSounds[1] = dev.RegisterSound
				("Sounds/Weapons/SMG/spV2Third2.wav");
			@spfireMediumSounds[2] = dev.RegisterSound
				("Sounds/Weapons/SMG/spV2Third3.wav");
			@spfireMediumSounds[3] = dev.RegisterSound
				("Sounds/Weapons/SMG/spV2Third4.wav");
			
			@fireMediumSounds[0] = dev.RegisterSound
				("Sounds/Weapons/SMG/V2Third1.wav");
			@fireMediumSounds[1] = dev.RegisterSound
				("Sounds/Weapons/SMG/V2Third2.wav");
			@fireMediumSounds[2] = dev.RegisterSound
				("Sounds/Weapons/SMG/V2Third3.wav");
			@fireMediumSounds[3] = dev.RegisterSound
				("Sounds/Weapons/SMG/V2Third4.wav");

			@fireFarSound = dev.RegisterSound
				("Sounds/Weapons/SMG/FireFar.opus");
			
			@reloadSound = dev.RegisterSound
				("Sounds/Weapons/SMG/Reload.wav");

		}

		void Update(float dt) {
		}

		void WeaponFired(){
			if(!muted){
				Vector3 origin = soundOrigin;
				AudioParam param;
				param.volume = 2.f;
				if(mp5_sp.FloatValue > 0){
					audioDevice.Play(spfireMediumSounds[GetRandom(fireMediumSounds.length)], origin, param);
				}else{
					audioDevice.Play(fireMediumSounds[GetRandom(fireMediumSounds.length)], origin, param);
				}
				
				param.volume = .2f;
				param.referenceDistance = 10.f;
				audioDevice.Play(fireFarSound,  origin, param);

			}
		}
		void ReloadingWeapon() {
			if(!muted){
				Vector3 origin = soundOrigin;
				AudioParam param;
				param.volume = 0.5f;
				audioDevice.Play(reloadSound, origin, param);
			}
		}

		void ReloadedWeapon() {
		}
		
		// Creates a rotation matrix from euler angles (in the form of a Vector3) x-y-z
        Matrix4 CreateEulerAnglesMatrix( Vector3 angles ) {
            Matrix4 mat = CreateRotateMatrix( Vector3(1.0, 0.0, 0.0), angles.x );
            mat = CreateRotateMatrix( Vector3(0.0, 1.0, 0.0), angles.y ) * mat;
            mat = CreateRotateMatrix( Vector3(0.0, 0.0, 1.0), angles.z ) * mat;
            return mat;
        }

		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(0.012f);
			mat = mat * CreateScaleMatrix(-1.f, -1.f, 1.f);
			mat = CreateTranslateMatrix(0.35f, -1.f, 0.0f) * mat;

			ModelRenderParam param;
			Matrix4 weapMatrix = originMatrix * mat;
			param.matrix = weapMatrix;
			
			Vector3 pivot = Vector3(15, 40, 0);
			
			// draw weapon
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.3f)
				* CreateTranslateMatrix(Vector3(-10.f, -20.f, 10.f)-pivot)
				* CreateEulerAnglesMatrix(Vector3(0.f, 1.55f, 0.f));
			renderer.AddModel(Weapon, param);
			
			// draw charginghandle
			param.matrix = weapMatrix 
				* CreateScaleMatrix(0.3f) 
				* CreateTranslateMatrix(Vector3(-7.f, 107.f, -61.f)-pivot);
			
			renderer.AddModel(chargingHandle, param);
			
			// draw frontsight
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.15f)
				* CreateTranslateMatrix(Vector3(-19.3f, 248.5f, -133.f)-pivot*2);
			renderer.AddModel(FrontSight, param);
			
			// draw magazine
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.3f)
				* CreateTranslateMatrix(Vector3(-11.f, 65.f, 20.f)-pivot)
				* CreateEulerAnglesMatrix(Vector3(0.f, 1.55f, 0.f));
			renderer.AddModel(MagFull, param);
			
			// draw suppressor
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.3f)
				* CreateTranslateMatrix(Vector3(-10.f, 130.f, -44.f)-pivot);
			
			if(mp5_sp.FloatValue > 0){
				renderer.AddModel(Suppressor, param);
			}
			
			// draw ironSight
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.15f)
				* CreateTranslateMatrix(Vector3(-19.5f, -35.f, -120.5f)-pivot*2);
			renderer.AddModel(ironSight, param);
		}
	}

	IWeaponSkin@ CreateThirdPersonSMGSkin(Renderer@ r, AudioDevice@ dev) {
		return ThirdPersonSMGSkin(r, dev);
	}
}
