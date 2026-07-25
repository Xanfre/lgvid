/*
 * comcompat.h
 *
 * Minimal definitions to improve source compatability of Windows software using
 * the common COM interface and function declaration macros for non-Windows
 * platforms.
 *
 * Using this will not maintain binary compatability with any Windows software,
 * nor will this produce sources identical to that produced using the original
 * headers. These are simply "as-is" replacements for the COM macros.
 *
 * Compiled from referencing the headers included with the Wine project:
 *     https://www.winehq.org/
 */

#ifndef COM_COMPAT_H
#define COM_COMPAT_H

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif
#endif

/* Microsoft's macros for declaring functions */

#define STDMETHODCALLTYPE
#define STDMETHODVCALLTYPE
#define STDAPICALLTYPE
#define STDAPIVCALLTYPE
#define STDAPI               EXTERN_C int
#define STDAPI_(type)        EXTERN_C type
#define STDMETHODIMP         int
#define STDMETHODIMP_(type)  type
#define STDAPIV              EXTERN_C int
#define STDAPIV_(type)       EXTERN_C type
#define STDMETHODIMPV        int
#define STDMETHODIMPV_(type) type

/* COM wrapper macros */

#define interface struct
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ interface */
#define STDMETHOD(method)        virtual int method
#define STDMETHOD_(type,method)  virtual type method
#define STDMETHODV(method)       virtual int method
#define STDMETHODV_(type,method) virtual type method

#define PURE  = 0
#define THIS_
#define THIS  void

#define DECLARE_INTERFACE(iface)        struct iface
#define DECLARE_INTERFACE_(iface,ibase) struct iface : public ibase
#define DECLARE_INTERFACE_IID(iface,iid) DECLARE_INTERFACE(iface)
#define DECLARE_INTERFACE_IID_(iface,ibase,iid) DECLARE_INTERFACE_(iface,ibase)

#else  /* __cplusplus && !CINTERFACE */

/* C interface */
#define STDMETHOD(method)        int (*method)
#define STDMETHOD_(type,method)  type (*method)
#define STDMETHODV(method)       int (*method)
#define STDMETHODV_(type,method) type (*method)

#define PURE
#define THIS_ INTERFACE *This,
#define THIS  INTERFACE *This

#define DECLARE_INTERFACE(iface) \
	typedef struct iface { struct iface##Vtbl *lpVtbl; } iface; \
	typedef struct iface##Vtbl iface##Vtbl; \
	struct iface##Vtbl
#define DECLARE_INTERFACE_(iface,ibase) DECLARE_INTERFACE(iface)
#define DECLARE_INTERFACE_IID(iface,iid) DECLARE_INTERFACE(iface)
#define DECLARE_INTERFACE_IID_(iface,ibase,iid) DECLARE_INTERFACE_(iface,ibase)

#endif  /* __cplusplus && !CINTERFACE */

#define BEGIN_INTERFACE
#define END_INTERFACE

#define IFACEMETHOD(method)         STDMETHOD(method)
#define IFACEMETHOD_(type,method)   STDMETHOD_(type,method)
#define IFACEMETHODV(method)        STDMETHODV(method)
#define IFACEMETHODV_(type,method)  STDMETHODV_(type,method)

#endif /* COM_COMPAT_H */
