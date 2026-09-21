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
		private Model@ gunModel1;
		private Model@ gunModel2;
		private Model@ magazineModel;
		
		private AudioChunk@[] fireSounds(4);
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ reloadSound;

		ViewSMGSkin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@gunModel1 = renderer.RegisterModel
				("Models/Weapons/SMG/1.kv6");
			@gunModel2 = renderer.RegisterModel
				("Models/Weapons/SMG/2.kv6");
			@magazineModel = renderer.RegisterModel
				("Models/Weapons/SMG/Magazine.kv6");
			
				
			@fireSounds[0] = dev.RegisterSound
				("Sounds/Weapons/SMG/V2Local1.wav");
			@fireSounds[1] = dev.RegisterSound
				("Sounds/Weapons/SMG/V2Local2.wav");
			@fireSounds[2] = dev.RegisterSound
				("Sounds/Weapons/SMG/V2Local3.wav");
			@fireSounds[3] = dev.RegisterSound
				("Sounds/Weapons/SMG/V2Local4.wav");
			@fireFarSound = dev.RegisterSound
				("Sounds/Weapons/SMG/FireFar.opus");
			@fireStereoSound = dev.RegisterSound
				("Sounds/Weapons/SMG/FireStereo.opus");
			@reloadSound = dev.RegisterSound
				("Sounds/Weapons/SMG/ReloadLocal.wav");

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
				audioDevice.PlayLocal(fireSounds[GetRandom(fireSounds.length)], origin, param);
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
			mat = CreateRotateMatrix(Vector3(0.f, 0.f, 1.f), atan(p.x / p.y) * fade) * mat;
			mat = CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), atan(p.z / p.y) * fade) * mat;
			return mat;
		}

		void Draw2D() {
			if(AimDownSightState > 0.6){
				Image@ img = renderer.RegisterImage("Gfx/smg.png");
				float height = renderer.ScreenHeight;
				float width = height * (800.f / 600.f); 
				renderer.Color = (Vector4(1.f, 1.f, 1.f, 1.f));
				renderer.DrawImage(img,
					AABB2((renderer.ScreenWidth - width) * 0.5f,
							(renderer.ScreenHeight - height) * 0.5f,
							width, height));
				return;
				}
			BasicViewWeapon::Draw2D();
		}

		void AddToScene() {
		    if(AimDownSightStateSmooth > 0.8){
				LeftHandPosition = Vector3(1.f, 6.f, 10.f);
				RightHandPosition = Vector3(0.f, -8.f, 20.f);
				return;
			}
			Matrix4 mat = CreateScaleMatrix(0.033f);
			mat = GetViewWeaponMatrix() * mat;

			bool reloading = IsReloading;
			float reload = ReloadProgress;
			Vector3 leftHand, rightHand;

			leftHand = mat * Vector3(1.f, 6.f, 2.f);
			rightHand = mat * Vector3(0.f, -8.f, 2.f);

			Vector3 leftHand2 = mat * Vector3(5.f, -10.f, 4.f);
			Vector3 leftHand3 = mat * Vector3(1.f, 6.f, -4.f);
			Vector3 leftHand4 = mat * Vector3(1.f, 9.f, -6.f);

			ModelRenderParam param;
			Matrix4 weapMatrix = eyeMatrix * mat;
			weapMatrix *= CreateTranslateMatrix(0.f, -5.f, 3.f);
			weapMatrix *= CreateScaleMatrix(0.1f);
			param.matrix = weapMatrix;
			param.depthHack = true;
			renderer.AddModel(gunModel2, param);
			
			
			// draw sights
			Matrix4 sightMat = weapMatrix;
			sightMat = weapMatrix;
			sightMat *= CreateTranslateMatrix(2.f, 7.f, -43.f);
			sightMat *= CreateScaleMatrix(1.f);
			param.matrix = sightMat;
			renderer.AddModel(gunModel1, param);

			// magazine/reload action
			mat *= CreateTranslateMatrix(0.f, 0.8f, 5.f);
			mat *= CreateScaleMatrix(0.1f);
			reload *= 2.5f;
			if(reloading) {
				if(reload < 0.7f){
					// magazine release
					float per = reload / 0.7f;
					mat *= CreateTranslateMatrix(0.f, 0.f, per*per*50.f);
					leftHand = Mix(leftHand, leftHand2, SmoothStep(per));
				}else if(reload < 1.4f) {
					// insert magazine
					float per = (1.4f - reload) / 0.7f;
					if(per < 0.3f) {
						// non-smooth insertion
						per *= 4.f; per -= 0.4f;
						per = Clamp(per, 0.0f, 0.3f);
					}

					mat *= CreateTranslateMatrix(0.f, 0.f, per*per*10.f);
					leftHand = mat * Vector3(0.f, 0.f, 4.f);
				}else if(reload < 1.9f){
					// move the left hand to the original position
					// and start doing something with the right hand
					float per = (reload - 1.4f) / 0.5f;
					leftHand = mat * Vector3(0.f, 0.f, 4.f);
					leftHand = Mix(leftHand, leftHand3, SmoothStep(per));
				}else if(reload < 2.2f){
					float per = (reload - 1.9f) / 0.3f;
					leftHand = Mix(leftHand3, leftHand4, SmoothStep(per));
				}else{
					float per = (reload - 2.2f) / 0.3f;
					leftHand = Mix(leftHand4, leftHand, SmoothStep(per));
				}
			}

			param.matrix = eyeMatrix * mat;
			renderer.AddModel(magazineModel, param);

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}

	}

	IWeaponSkin@ CreateViewSMGSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewSMGSkin(r, dev);
	}
}
