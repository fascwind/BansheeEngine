using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace BansheeEngine
{
	/** @addtogroup Rendering
	 *  @{
	 */

	/// <summary>Allows you to specify an environment map to use for sampling radiance of the sky.</summary>
	public partial class Skybox : Component
	{
		private Skybox(bool __dummy0) { }
		protected Skybox() { }

		/// <summary>
		/// Determines an environment map to use for sampling skybox radiance. Must be a cube-map texture, and should ideally 
		/// contain HDR data.
		/// </summary>
		[ShowInInspector]
		public Texture Texture
		{
			get { return Internal_getTexture(mCachedPtr); }
			set { Internal_setTexture(mCachedPtr, value); }
		}

		/// <summary>
		/// Brightness multiplier that will be applied to skybox values before they're being used. Allows you to make the skybox 
		/// more or less bright. Equal to one by default.
		/// </summary>
		[ShowInInspector]
		public float Brightness
		{
			get { return Internal_getBrightness(mCachedPtr); }
			set { Internal_setBrightness(mCachedPtr, value); }
		}

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern Texture Internal_getTexture(IntPtr thisPtr);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_setTexture(IntPtr thisPtr, Texture texture);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern void Internal_setBrightness(IntPtr thisPtr, float brightness);
		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern float Internal_getBrightness(IntPtr thisPtr);
	}

	/** @} */
}
