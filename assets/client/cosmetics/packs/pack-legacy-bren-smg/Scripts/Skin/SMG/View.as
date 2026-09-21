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
	class ViewSMGSkin:
	IToolSkin, IViewToolSkin, IWeaponSkin, IWeaponSkin2,
	BasicViewWeapon {

		private AudioDevice@ audioDevice;
		private Model@ gunModel;
		private Model@ magazineModel;
		private Model@ barrelModel;
		private Model@ stockModel;
		private Model@ sightModel1;
		private Model@ sightModel2;
		private Model@ sightModel3;

		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;
		private ConfigItem@ cg_fov = ConfigItem("cg_fov");
		
		
		// pivot of the weapon when viewed in slab6
		private Vector3 pivot = Vector3(3.50, 33.0, 23.0);
		// scale of the weapon
		private float globalScale = 0.01;
		// scale of the magazine, relative to the global scale
		private float magazineScale = 0.5;
		// delta time between each frame
		private float deltatime = 0.0;
		// checks if mag has been thrown
		// reset to false whenever reloading
		private bool hasThrownMag = false;
		
		// Creates a rotation matrix from euler angles (in the form of a Vector3) x-y-z
		Matrix4 CreateEulerAnglesMatrix( Vector3 angles ) {
			Matrix4 mat = CreateRotateMatrix( Vector3(1.0, 0.0, 0.0), angles.x );
			mat = CreateRotateMatrix( Vector3(0.0, 1.0, 0.0), angles.y ) * mat;
			mat = CreateRotateMatrix( Vector3(0.0, 0.0, 1.0), angles.z ) * mat;
			
			return mat;
		}
	
		// select easing functions
		float quadraticIn(float per) {
			return (per*per);
		}
		
		float quadraticOut(float per) {
			return -(per * (per-2));
		}
		
		float quadraticInOut(float per) {
			per < per/2; 
			return (per*per);
			
			per > per/2; 
			return -(per * (per-2));
			
		}	
		
		float cubicIn(float per) {
			return (per*per*per);
		}
		
		float cubicOut(float per) {
			per = per-1;
			return (per*per*per + 1);
		}

		ViewSMGSkin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				("Models/Weapons/SMG/WeaponNoMag.kv6");
			@magazineModel = renderer.RegisterModel
				("Models/Weapons/SMG/magazine.kv6");
			@barrelModel = renderer.RegisterModel
				("Models/Weapons/SMG/barrelNbipod.kv6");
			@stockModel = renderer.RegisterModel
				("Models/Weapons/SMG/stock.kv6");
			@sightModel1 = renderer.RegisterModel
				("Models/Weapons/SMG/Sight1.kv6");
			@sightModel2 = renderer.RegisterModel
				("Models/Weapons/SMG/Sight2.kv6");
			@sightModel3 = renderer.RegisterModel
				("Models/Weapons/SMG/Sight3.kv6");

						@fireSound = dev.RegisterSound
				("Sounds/Weapons/SMG/FireLocal.wav");
			@fireFarSound = dev.RegisterSound
				("Sounds/Weapons/SMG/Fire3rd.wav");
			@fireStereoSound = dev.RegisterSound
				("Sounds/Weapons/SMG/FireLocal.wav");
			@reloadSound = dev.RegisterSound
				("Sounds/Weapons/SMG/ReloadLocal.wav");

			@fireSmallReverbSound = dev.RegisterSound
				("Sounds/Weapons/SMG/Fire3rd.wav");
			@fireLargeReverbSound = dev.RegisterSound
				("Sounds/Weapons/SMG/Fire3rd.wav");
		}

		void Update(float dt) {
			BasicViewWeapon::Update(dt);
		}

		void WeaponFired(){
			BasicViewWeapon::WeaponFired();

			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 8.f;
				audioDevice.PlayLocal(fireSound, origin, param);

				param.volume = 8.f * environmentRoom;
				if (environmentSize < 0.5f) {
					audioDevice.PlayLocal(fireSmallReverbSound, origin, param);
				} else {
					audioDevice.PlayLocal(fireLargeReverbSound, origin, param);
				}

				param.referenceDistance = 4.f;
				param.volume = 1.f;
				audioDevice.PlayLocal(fireFarSound, origin, param);
				param.referenceDistance = 1.f;
				audioDevice.PlayLocal(fireStereoSound, origin, param);
			}
		}

		void ReloadingWeapon() {
			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 0.2f;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}
		}

		float GetZPos() {
			return 0.2f - AimDownSightStateSmooth * 0.038f;
		}

		// rotates gun matrix to ensure the sight is in
		// the center of screen (0, ?, 0).
		Matrix4 AdjustToAlignSight(Matrix4 mat, Vector3 sightPos, float fade) {
			Vector3 p = mat * sightPos;
			mat = CreateRotateMatrix(Vector3(0.f, 0.f, 0.f), atan(p.x / p.y) * fade) * mat;
			mat = CreateRotateMatrix(Vector3(0.f, 0.f, 0.f), atan(p.z / p.y) * fade) * mat;
			return mat;
		}

		void Draw2D() {
			if(AimDownSightState > 0.6)
				return;
			BasicViewWeapon::Draw2D();
		}
		
				// redefined from BasicViewWeapon.as
		Matrix4 GetViewWeaponMatrix() {	
			Matrix4 mat;
			// sprinting animation					
			if(sprintState > 0.0) {
				sprintState = quadraticIn(sprintState);
				mat = CreateEulerAnglesMatrix(Vector3(0.44, -0.0, -0.5)*sprintState) * mat;
				mat = CreateTranslateMatrix(Vector3(0.1, 0.0, 0.2)*sprintState) * mat;
			}
			
			// raise gun animation
			if(raiseState < 1.0) {
				float putdown = 1.0 - raiseState;
				putdown = cubicIn(putdown);
				mat = CreateRotateMatrix(Vector3(0.0, 0.0, 1.0),
					putdown * -1.3) * mat;
				mat = CreateRotateMatrix(Vector3(1.0, 0.0, 0.0),
					putdown * 0.2) * mat;
				mat = CreateTranslateMatrix(Vector3(0.1, -0.3, 0.8)
					* putdown) * mat;
			}
			
			// recoil animation
			Vector3 recoilRot;
			Vector3 recoilOffset;
			if(readyState < 0.1) {
				float per = (readyState/0.1);
				per = cubicOut(per);
				recoilRot = Vector3(0.0, 0.0, 0.0) * per;
				recoilOffset = Vector3(0.0, -0.02, -0.0) * per;
			} else if(readyState < 0.4) {
				float per = ( (readyState-0.1)/(0.4-0.1) );
				per = SmoothStep(per);
				recoilRot = Mix(Vector3(0.0, 0.0, 0.0), Vector3 (0.01, 0.0, 0.0), per);
				recoilOffset = Mix(Vector3(0.0, -0.02, -0.0), Vector3(0.0, 0.025, 0.0), per);
			} else if(readyState < 0.8) {
				float per = ( (readyState-0.4)/(0.8-0.4) );
				per = SmoothStep(per);
				recoilRot = Mix(Vector3(0.01, 0.0, 0.0), Vector3 (0.0, 0.0, 0.0), per);
				recoilOffset = Mix(Vector3(0.0, -0.025, -0.0), Vector3(0.0, 0.0, 0.0), per);
			} else if(readyState < 1.0) {
				float per = ( (readyState-0.8)/(1.0-0.8) );
				per = SmoothStep(per);
				recoilOffset = Vector3(0.0, 0.0, 0.0);
			}
			// No recoil when the player is aiming. Multiply by (1 - aimScopingState)
			float unSightState = 1.0-AimDownSightStateSmooth;
			mat = CreateEulerAnglesMatrix(recoilRot*1) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset*1);
			
			// default offset from when the player is not aiming (i.e. default position)
			mat = CreateTranslateMatrix( Mix( Vector3(-0.13, 0.5,0.2), Vector3(0.0, 0.35, -(0.-pivot.z/1.4)*globalScale), AimDownSightStateSmooth)) * mat; 

			// offset from when the player is walking
			// again, don't move the gun when the weapon is aimed
			mat = CreateTranslateMatrix(swing * GetMotionGain() * 1.8) * mat;

			// twist the gun when strafing
			// don't rotate when scoped
			mat = mat * CreateEulerAnglesMatrix(Vector3(0.0, 2.0*swing.x, 0.0)*1);
			
			return mat;
		}

		Vector3 AnimHandLeft(Matrix4 mat , Vector3 leftHand ) {
		
		if (reloadProgress < 0.1) { //hand animations
				float per = ( (reloadProgress-0.0)/(0.1-0.0) );
				per = quadraticInOut(per);
				rightHand = mat * (Mix(Vector3(-2.f, -5.f, 4.f),Vector3(1.f, -5.f, 4.f), per));
				leftHand = mat * (Mix( Vector3(6.f, 6.5f, -1.f), Vector3(3.f, 7.5f, -2.f), per));
				
					}

		else if (reloadProgress < 0.88) { //right hand pulls charging handle
				float per = ( (reloadProgress-0.10)/(0.88-0.10) );
				per = quadraticInOut(per);
				leftHand = mat * (Mix( Vector3(3.f, 7.5f, -2.f), Vector3(3.f, 7.5f, -2.f), per));
				
		}	
		else if (reloadProgress < 1.0) { //right hand lets go of charging handle and goes back to the weapon
							float per = ( (reloadProgress-0.88)/(1.0-0.88) );
							per = quadraticInOut(per);
							rightHand = mat * (Mix( Vector3(-1.f, -3.f, 4.f), Vector3(-2.f, -5.f, 4.f), per));
							leftHand = mat * (Mix( Vector3(3.f, 7.5f, -2.f), Vector3(6.f, 6.5f, -1.f), per));
		}
	return leftHand;
	}
		Vector3 AnimHandRight(Matrix4 mat , Vector3 rightHand) {
		
		if (reloadProgress < 0.1) { //hand animations
				float per = ( (reloadProgress-0.0)/(0.1-0.0) );
				per = quadraticInOut(per);
				rightHand = mat * (Mix(Vector3(-2.f, -5.f, 4.f),Vector3(1.f, -5.f, 4.f), per));
				
					}
		else if (reloadProgress < 0.20) { //right hand moves to magazine
				float per = ( (reloadProgress-0.1)/(0.20-0.1) );
				per = quadraticInOut(per);
				rightHand = mat * (Mix( Vector3(1.f, -5.f, 4.f), Vector3(.0, 3.0, -8.0), per));
				
		}
		else if (reloadProgress < 0.30) { //right hand pulls out magazine
				float per = ( (reloadProgress-0.20)/(0.30-0.20) );
				per = quadraticOut(per);
				rightHand = mat * (Mix( Vector3(.0, 3.0, -8.0), Vector3(-1.0, 3.5, -8.5), per));
			
		}
		else if (reloadProgress < 0.45) { //right hand gets magazine off the screen
				float per = ( (reloadProgress-0.30)/(0.45-0.30) );
				per = SmoothStep(per);
				rightHand = mat * (Mix( Vector3(-1.0, 3.5, -8.5), Vector3(-28.0, 3.5, 5.5), per));
				
		}
		else if (reloadProgress < 0.60) { //right hand comes back with new magazine
				float per = ( (reloadProgress-0.45)/(0.60-0.45) );
				per = SmoothStep(per);
				rightHand = mat * (Mix( Vector3(-28.0, 3.5, 5.5), Vector3(-1.0, 3.5, -8.5), per));
				
				
		}
		else if (reloadProgress < 0.70) { //right hand clicks new magazine in place
				float per = ( (reloadProgress-0.60)/(0.70-0.60) );
				per = quadraticIn(per);
				rightHand = mat * (Mix( Vector3(-1.0, 3.5, -8.5), Vector3(.0, 3.5, -8.0), per));
				
		}			
		else if (reloadProgress < 0.80) { //right hand moves to charging handle
				float per = ( (reloadProgress-0.70)/(0.80-0.70) );
				per = quadraticInOut(per);
				rightHand = mat * (Mix( Vector3(.0, 3.5, -8.0), Vector3(1.f, 1.f, 4.f), per));
				
		}
		else if (reloadProgress < 0.88) { //right hand pulls charging handle
				float per = ( (reloadProgress-0.80)/(0.88-0.80) );
				per = quadraticIn(per);
				rightHand = mat * (Mix( Vector3(1.f, 1.f, 4.f), Vector3(-1.0f, -3.f, 4.f), per));
				
		}	
		else if (reloadProgress < 1.0) { //right hand lets go of charging handle and goes back to the weapon
							float per = ( (reloadProgress-0.88)/(1.0-0.88) );
							per = quadraticInOut(per);
							rightHand = mat * (Mix( Vector3(-1.f, -3.f, 4.f), Vector3(-2.f, -5.f, 4.f), per));
		}
	return rightHand;
	}
		void AnimWeap(Matrix4 mat , Matrix4 weapMatrix , ModelRenderParam param) {
		
				
			
	if (reloadProgress < 0.1) { //rotate the gun down and left for left hand hold on the frame
				float per = ( (reloadProgress-0.0)/(0.1-0.0) );
				per = quadraticInOut(per);
				
				
				mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0., 0., 0.) , Vector3(-.5, 0.80, -0.25), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(.0, .0, .0), Vector3(3.0f, 3.0f, 4.0f), per));
				
				weapMatrix = eyeMatrix * mat; //this is to add in the models, i got 3 to make the weapon itself
				param.matrix = weapMatrix
				* CreateScaleMatrix(0.25f, 0.15f, 0.25f);
				renderer.AddModel(gunModel, param);
				renderer.AddModel(barrelModel, param);
				renderer.AddModel(stockModel, param);
				renderer.AddModel(magazineModel, param);
				
							// draw sights
				Matrix4 sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, -6.71f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel1, param); // rear


				sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, 17.35f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
							renderer.AddModel(sightModel2, param); // front
					}
		else if (reloadProgress < 0.20) { //hold the gun in place during the reload (left hand)
				float per = ( (reloadProgress-0.1)/(0.20-0.1) );
				per = quadraticInOut(per);
				
				
				mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-.5f, 0.80f, -0.25f) , Vector3(-.5f, 0.80f, -0.25f), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(3.0f, 3.0f, 4.0f), Vector3(3.0f, 3.0f, 4.0f), per));
					
				weapMatrix = eyeMatrix * mat; 
				param.matrix = weapMatrix
				* CreateScaleMatrix(0.25f, 0.15f, 0.25f);
				renderer.AddModel(gunModel, param);
				renderer.AddModel(barrelModel, param);
				renderer.AddModel(stockModel, param);
				renderer.AddModel(magazineModel, param);
				
							// draw sights
				Matrix4 sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, -6.71f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel1, param); // rear


				sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, 17.35f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel2, param); // front
		}
		else if (reloadProgress < 0.30) { //mag gets pulled out
				float per = ( (reloadProgress-0.20)/(0.30-0.20) );
				per = quadraticIn(per);
				
				
				mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-.5f, 0.80f, -0.25f) , Vector3(-.5f, 0.80f, -0.25f), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(3.0f, 3.0f, 4.0f), Vector3(3.0f, 3.0f, 4.0f), per));
					
				weapMatrix = eyeMatrix * mat; 
				param.matrix = weapMatrix
				* CreateScaleMatrix(0.25f, 0.15f, 0.25f);
				renderer.AddModel(gunModel, param);
				renderer.AddModel(barrelModel, param);
				renderer.AddModel(stockModel, param);
				
				param.matrix = weapMatrix *	CreateScaleMatrix(0.25f, 0.15f, 0.25f) 
				* CreateEulerAnglesMatrix( Mix (Vector3(0.0, 0.0, 0.0) , Vector3(-0.1, 0.0, 0.1), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(0.0, 0.0, 0.0), Vector3(-2.0, 0.0, -5.0), per));
				renderer.AddModel(magazineModel, param);
				
							// draw sights
				Matrix4 sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, -6.71f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel1, param); // rear


				sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, 17.35f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel2, param); // front
		}
		else if (reloadProgress < 0.45) { //mag gets off screen
				float per = ( (reloadProgress-0.30)/(0.45-0.30) );
				per = SmoothStep(per);
				
				
				mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-.5f, 0.80f, -0.25f) , Vector3(-.5f, 0.80f, -0.25f), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(3.0f, 3.0f, 4.0f), Vector3(3.0f, 3.0f, 4.0f), per));
					
				weapMatrix = eyeMatrix * mat; 
				param.matrix = weapMatrix
				* CreateScaleMatrix(0.25f, 0.15f, 0.25f);
				renderer.AddModel(gunModel, param);
				renderer.AddModel(barrelModel, param);
				renderer.AddModel(stockModel, param);
				
				param.matrix = weapMatrix *	CreateScaleMatrix(0.25f, 0.15f, 0.25f) 
				* CreateEulerAnglesMatrix( Mix (Vector3(-0.1, 0.0, 0.1) , Vector3(-0.1, 0.0, 0.1), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(-2.0, 0.0, -5.0), Vector3(-150.0, 0.0, -75.0), per));
				renderer.AddModel(magazineModel, param);
				
							// draw sights
				Matrix4 sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, -6.71f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel1, param); // rear


				sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, 17.35f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel2, param); // front
		}
		else if (reloadProgress < 0.60) { //new mag comes in screen
				float per = ( (reloadProgress-0.45)/(0.60-0.45) );
				per = SmoothStep(per);
				
				
				mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-.5f, 0.80f, -0.25f) , Vector3(-.5f, 0.80f, -0.25f), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(3.0f, 3.0f, 4.0f), Vector3(3.0f, 3.0f, 4.0f), per));
					
				weapMatrix = eyeMatrix * mat; 
				param.matrix = weapMatrix
				* CreateScaleMatrix(0.25f, 0.15f, 0.25f);
				renderer.AddModel(gunModel, param);
				renderer.AddModel(barrelModel, param);
				renderer.AddModel(stockModel, param);
				
				param.matrix = weapMatrix *	CreateScaleMatrix(0.25f, 0.15f, 0.25f) 
				* CreateEulerAnglesMatrix( Mix (Vector3(-0.1, 0.0, 0.1) , Vector3(-0.1, 0.0, 0.1), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(-150.0, 0.0, -75.0), Vector3(-2.0, 0.0, -5.0) , per));
				renderer.AddModel(magazineModel, param);
				
							// draw sights
				Matrix4 sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, -6.71f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel1, param); // rear


				sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, 17.35f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel2, param); // front
				
		}
		else if (reloadProgress < 0.70) { //new mag gets clicked in
				float per = ( (reloadProgress-0.60)/(0.70-0.60) );
				per = quadraticIn(per);
				
				
				mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-.5f, 0.80f, -0.25f) , Vector3(-.5f, 0.80f, -0.25f), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(3.0f, 3.0f, 4.0f), Vector3(3.0f, 3.0f, 4.0f), per));
					
				weapMatrix = eyeMatrix * mat; 
				param.matrix = weapMatrix
				* CreateScaleMatrix(0.25f, 0.15f, 0.25f);
				renderer.AddModel(gunModel, param);
				renderer.AddModel(barrelModel, param);
				renderer.AddModel(stockModel, param);
				
				param.matrix = weapMatrix *	CreateScaleMatrix(0.25f, 0.15f, 0.25f) 
				* CreateEulerAnglesMatrix( Mix (Vector3(-0.1, 0.0, 0.1) , Vector3(0.0, 0.0, 0.0), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(-2.0, 0.0, -5.0), Vector3(0.0, 0.0, 0.0), per));
				renderer.AddModel(magazineModel, param);
				
							// draw sights
				Matrix4 sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, -6.71f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel1, param); // rear


				sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, 17.35f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel2, param); // front
		}
		else if (reloadProgress < 0.80) { //hand moves to charging handle
				float per = ( (reloadProgress-0.70)/(0.80-0.70) );
				per = quadraticIn(per);
				
				
				mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-.5f, 0.80f, -0.25f) , Vector3(-.5f, 0.80f, -0.25f), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(3.0f, 3.0f, 4.0f), Vector3(3.0f, 3.0f, 4.0f), per));
					
				weapMatrix = eyeMatrix * mat; 
				param.matrix = weapMatrix
				* CreateScaleMatrix(0.25f, 0.15f, 0.25f);
				renderer.AddModel(gunModel, param);
				renderer.AddModel(barrelModel, param);
				renderer.AddModel(stockModel, param);
				renderer.AddModel(magazineModel, param);
				
							// draw sights
				Matrix4 sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, -6.71f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel1, param); // rear


				sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, 17.35f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel2, param); // front
		}
		else if (reloadProgress < 0.88) { //charging handle pulled
				float per = ( (reloadProgress-0.80)/(0.88-0.80) );
				per = quadraticInOut(per);
				
				
				mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-.5f, 0.80f, -0.25f) , Vector3(-.5f, 0.80f, -0.25f), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(3.0f, 3.0f, 4.0f), Vector3(3.0f, 3.0f, 4.0f), per));
				
				weapMatrix = eyeMatrix * mat; //this is to add in the models, i got 3 to make the weapon itself
				param.matrix = weapMatrix
				* CreateScaleMatrix(0.25f, 0.15f, 0.25f);
				renderer.AddModel(gunModel, param);
				renderer.AddModel(barrelModel, param);
				renderer.AddModel(stockModel, param);
				renderer.AddModel(magazineModel, param);
				
							// draw sights
				Matrix4 sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, -6.71f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel1, param); // rear


				sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, 17.35f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel2, param); // front
			}
		else if (reloadProgress < 1.0) { //weapon goes back to default position
				float per = ( (reloadProgress-0.88)/(1.0-0.88) );
				per = quadraticInOut(per);
				
				mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-.5f, 0.80f, -0.25f) , Vector3(0.0,0.0, 0.0), per )) 
				 * CreateTranslateMatrix(Mix (Vector3(3.0f, 3.0f, 4.0f), Vector3(0.0, 0.0, 0.0), per));
				
				weapMatrix = eyeMatrix * mat; //this is to add in the models, i got 3 to make the weapon itself
				param.matrix = weapMatrix
				* CreateScaleMatrix(0.25f, 0.15f, 0.25f);
				renderer.AddModel(gunModel, param);
				renderer.AddModel(barrelModel, param);
				renderer.AddModel(stockModel, param);
				renderer.AddModel(magazineModel, param);
				
							// draw sights
				Matrix4 sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, -6.71f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel1, param); // rear


				sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, 17.35f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel2, param); // front
				
				
			}
			
				
	}
		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(0.033);
			mat = GetViewWeaponMatrix() * mat;

			bool reloading = IsReloading;
			float reload = reloadProgress;
			Vector3 leftHand, rightHand;

			leftHand = mat * Vector3(6.f, 6.5f, -1.f);
			rightHand = mat * Vector3(-2.f, -5.f, 4.f);
			


			ModelRenderParam param;
				Matrix4 weapMatrix = eyeMatrix * mat;
				param.matrix = weapMatrix * CreateScaleMatrix (0.25f, 0.15f, 0.25f);
				param.depthHack = true;

				if(AimDownSightStateSmooth > 0.8f){
					mat = AdjustToAlignSight(mat, Vector3(0.f, 0.f, .0f), (AimDownSightStateSmooth - 0.8f) / 0.2f);
				}

				if(reloadProgress < 1.){
				leftHand=AnimHandLeft(mat , leftHand );
				rightHand=AnimHandRight(mat , rightHand);
				AnimWeap(mat , weapMatrix , param);
				}
				
				else{
				
				(!reloading);
				renderer.AddModel(gunModel, param);
				renderer.AddModel(barrelModel, param);
				renderer.AddModel(stockModel, param);
				renderer.AddModel(magazineModel, param);
				
				// draw sights
				Matrix4 sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, -6.71f, -4.62f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel1, param); // rear


				sightMat = weapMatrix;
				sightMat *= CreateTranslateMatrix(0.01f, 17.35f, -4.55f);
				sightMat *= CreateScaleMatrix(0.042f);
				param.matrix = sightMat;
				renderer.AddModel(sightModel2, param); // front
				}

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
			
		}
	}

	IWeaponSkin@ CreateViewSMGSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewSMGSkin(r, dev);
	}

}
