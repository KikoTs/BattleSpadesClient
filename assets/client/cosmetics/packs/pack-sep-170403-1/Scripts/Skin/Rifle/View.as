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
	class ViewRifleSkin: 
	IToolSkin, IViewToolSkin, IWeaponSkin,
	BasicViewWeapon {
		
		private AudioDevice@ audioDevice;
		private Model@ gunModel;
		private Model@ magazineModel;
		private Model@ scopeModel;
		
		private Image@ reflexImage;
		
		private AudioChunk@ fireSounds;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ reloadSound;
		
		ViewRifleSkin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				("Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			@magazineModel = renderer.RegisterModel
				("Models/Weapons/Rifle/Magazine.kv6");
			@scopeModel = renderer.RegisterModel
				("Models/Weapons/Rifle/Scope.kv6");
			@reflexImage = renderer.RegisterImage
				("Gfx/RifleSight.png");
				
			@fireSounds = dev.RegisterSound
				("Sounds/Weapons/Rifle/Fire.wav");
			@fireFarSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireFar.wav");
			@fireStereoSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireStereo.wav");
			@reloadSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/ReloadLocal.wav");
				
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
				audioDevice.PlayLocal(fireSounds, origin, param);
				param.volume = 0.7f;
				audioDevice.PlayLocal(fireFarSound, origin, param);
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
			return 0.2f - AimDownSightStateSmooth * -0.04f;
		}
		
		// rotates gun matrix to ensure the sight is in
		// the center of screen (0, ?, 0).
		// backported for 0.0.9.
		Matrix4 AdjustToAlignSight(Matrix4 mat, Vector3 sightPos, float fade) {
			Vector3 p = mat * sightPos;
			float x = Dot(p, Vector3(1.f, 0.f, 0.f));
			float y = Dot(p, Vector3(0.f, 1.f, 0.f));
			float z = Dot(p, Vector3(0.f, 0.f, 0.f));
			mat = CreateRotateMatrix(Vector3(0.f, 0.f, 1.f), atan(x / y) * fade) * mat;
			mat = CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), atan(z / y) * fade) * mat;
			return mat;
		}
		
		void Draw2D() {
			if(AimDownSightState > 0.6)
				return;
			BasicViewWeapon::Draw2D();
		}
		
		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(0.012f);
			mat = GetViewWeaponMatrix() * mat;
			
			bool reloading = IsReloading;
			float reload = ReloadProgress;
			Vector3 leftHand, rightHand;
			
			leftHand = mat * Vector3(1.f, 20.f, 5.f );
			rightHand = mat * Vector3(0.f, -20.f, 6.5f);
			
			Vector3 leftHand2 = mat * Vector3(5.f, -10.f, 15.f);
			Vector3 rightHand3 = mat * Vector3(-2.f, -7.f, -4.f);
			Vector3 rightHand4 = mat * Vector3(-3.f, -4.f, -6.f);
			
			// stick the scope to the center of screen
			if(AimDownSightStateSmooth > 0.8f){
				mat = AdjustToAlignSight(mat, Vector3(0.f, -8.f, -5.75f), (AimDownSightStateSmooth - 0.8f) / 0.2f);
			}
			
			ModelRenderParam param;
			Matrix4 weapMatrix = eyeMatrix * mat;
			param.matrix = weapMatrix;
			param.depthHack = true;
			renderer.AddModel(gunModel, param);
			
			// draw sights
			Matrix4 sightMat = weapMatrix;
			sightMat *= CreateTranslateMatrix(0.25f, -1.f, -20.10f);
			sightMat *= CreateScaleMatrix(0.5f);
			param.matrix = sightMat;
			renderer.AddModel(scopeModel, param);
			
			// draw reflex image
			float reflexOpacity = (AimDownSightState > 0.8f) ? AimDownSightState * 5.f - 4.f : 0.f;
			Vector3 reflexPos = eyeMatrix * Vector3(0.f, 0.2f, 0.f);
			renderer.Color = Vector4(reflexOpacity, reflexOpacity, reflexOpacity, 0.f); // premultiplied alpha
			renderer.AddLongSprite(reflexImage, reflexPos, reflexPos, 0.004f);
			
			// magazine/reload action
			mat *= CreateTranslateMatrix(0.f, 1.f, 1.f);
			reload *= 2.5f;
			if(reloading) {
				if(reload < 0.1f){
					// move hand to magazine
					float per = reload / 0.1f;
					leftHand = Mix(leftHand,
						mat * Vector3(0.f, 0.f, 15.f),
						SmoothStep(per));
				}else if(reload < 0.7f){
					// magazine release
					float per = (reload - 0.1f) / 0.6f;
					if(per < 0.2f){
						// non-smooth pull out
						per *= 4.0f; per -= 0.4f;
						per = Clamp(per, 0.0f, 0.2f);
					}
					if(per > 0.5f) {
						// when per = 0.5f, the hand no longer holds the magazine,
						// so the free fall starts
						per += per - 0.5f;
					}
					mat *= CreateTranslateMatrix(0.f, 0.f, per*per*15.f);
					
					leftHand = mat * Vector3(0.f, 0.f, 15.f);
					if(per > 0.5f){
						per = (per - 0.5f);
						leftHand = Mix(leftHand, leftHand2, SmoothStep(per));
					}
				}else if(reload < 1.4f) {
					// insert magazine
					float per = (1.4f - reload) / 0.7f;
					if(per < 0.3f) {
						// non-smooth insertion
						per *= 4.f; per -= 0.4f;
						per = Clamp(per, 0.0f, 0.3f);
					}
					
					mat *= CreateTranslateMatrix(0.f, 0.f, per*per*15.f);
					leftHand = mat * Vector3(0.f, 0.f, 15.f);
				}else if(reload < 1.9f){
					// move the left hand to the original position
					// and start doing something with the right hand
					float per = (reload - 1.4f) / 0.5f;
					Vector3 orig = leftHand;
					leftHand = mat * Vector3(0.f, 0.f, 15.f);
					leftHand = Mix(leftHand, orig, SmoothStep(per));
					rightHand = Mix(rightHand, rightHand3, SmoothStep(per));
				}else if(reload < 2.2f){
					float per = (reload - 1.9f) / 0.3f;
					rightHand = Mix(rightHand3, rightHand4, SmoothStep(per));
				}else{
					float per = (reload - 2.2f) / 0.3f;
					rightHand = Mix(rightHand4, rightHand, SmoothStep(per));
				}
			}
			
			param.matrix = eyeMatrix * mat;
			renderer.AddModel(magazineModel, param);
			
			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
		
	}
	
	IWeaponSkin@ CreateViewRifleSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewRifleSkin(r, dev);
	}
}
