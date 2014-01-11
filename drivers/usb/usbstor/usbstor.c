/*
 * PROJECT:     ReactOS Universal Serial Bus Bulk Storage Driver
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/usb/usbstor/usbstor.c
 * PURPOSE:     USB block storage device driver.
 * PROGRAMMERS:
 *              James Tabor
                Johannes Anderwald
 */

/* INCLUDES ******************************************************************/

#include "usbstor.h"

#define NDEBUG
#include <debug.h>

/* PUBLIC AND PRIVATE FUNCTIONS **********************************************/

NTSTATUS
NTAPI
USBSTOR_AddDevice(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject;
    PFDO_DEVICE_EXTENSION DeviceExtension;

    //
    // lets create the device
    //
    Status = IoCreateDevice(DriverObject, sizeof(FDO_DEVICE_EXTENSION), 0, FILE_DEVICE_BUS_EXTENDER, FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);

    //
    // check for success
    //
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBSTOR_AddDevice: Failed to create FDO Status %x\n", Status);
        return Status;
    }

    //
    // get device extension
    //
    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(DeviceExtension);

    //
    // zero device extension
    //
    RtlZeroMemory(DeviceExtension, sizeof(FDO_DEVICE_EXTENSION));

    //
    // initialize device extension
    //
    DeviceExtension->Common.IsFDO = TRUE;
    DeviceExtension->FunctionalDeviceObject = DeviceObject;
    DeviceExtension->PhysicalDeviceObject = PhysicalDeviceObject;
    DeviceExtension->LowerDeviceObject = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);

    //
    // init timer
    //
    IoInitializeTimer(DeviceObject, USBSTOR_TimerRoutine, (PVOID)DeviceExtension);

    //
    // did attaching fail
    //
    if (!DeviceExtension->LowerDeviceObject)
    {
        //
        // device removed
        //
        IoDeleteDevice(DeviceObject);

        return STATUS_DEVICE_REMOVED;
    }

    //
    // set device flags
    //
    DeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;

    //
    // device is initialized
    //
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;


    //
    // done
    //
    return STATUS_SUCCESS;
}

VOID
NTAPI
USBSTOR_Unload(
    PDRIVER_OBJECT DriverObject)
{
    //
    // no-op
    //
}

NTSTATUS
NTAPI
USBSTOR_DispatchClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    //
    // function always succeeds ;)
    //
    DPRINT("USBSTOR_DispatchClose\n");
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}


NTSTATUS
NTAPI
USBSTOR_DispatchDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    NTSTATUS Status;

    //
    // handle requests
    //
    Status = USBSTOR_HandleDeviceControl(DeviceObject, Irp);

    //
    // complete request
    //
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    //
    // done
    //
    return Status;
}


NTSTATUS
NTAPI
USBSTOR_DispatchScsi(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    //
    // handle requests
    //
    return USBSTOR_HandleInternalDeviceControl(DeviceObject, Irp);
}

NTSTATUS
NTAPI
USBSTOR_DispatchReadWrite(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    //
    // read write ioctl is not supported
    //
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS
NTAPI
USBSTOR_DispatchPnp(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PUSBSTOR_COMMON_DEVICE_EXTENSION DeviceExtension;

    //
    // get common device extension
    //
    DeviceExtension = (PUSBSTOR_COMMON_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    //
    // is it for the FDO
    //
    if (DeviceExtension->IsFDO)
    {
        //
        // dispatch pnp request to fdo pnp handler
        //
        return USBSTOR_FdoHandlePnp(DeviceObject, Irp);
    }
    else
    {
        //
        // dispatch request to pdo pnp handler
        //
        return USBSTOR_PdoHandlePnp(DeviceObject, Irp);
    }
}

NTSTATUS
NTAPI
USBSTOR_DispatchPower(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;

    // get common device extension
    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (DeviceExtension->Common.IsFDO)
    {
        PoStartNextPowerIrp(Irp);
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(DeviceExtension->LowerDeviceObject, Irp);
    }
    else
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }
}



NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegPath)
{

    DPRINT("********* USB Storage *********\n");

    //
    // driver unload routine
    //
    DriverObject->DriverUnload = USBSTOR_Unload;

    //
    // add device function
    //
    DriverObject->DriverExtension->AddDevice = USBSTOR_AddDevice;

    //
    // driver start i/o routine
    //
    DriverObject->DriverStartIo = USBSTOR_StartIo;

    //
    // create / close
    //
    DriverObject->MajorFunction[IRP_MJ_CREATE] = USBSTOR_DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = USBSTOR_DispatchClose;

    //
    // scsi pass through requests
    //
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = USBSTOR_DispatchDeviceControl;

    //
    // irp dispatch read / write
    //
    DriverObject->MajorFunction[IRP_MJ_READ] = USBSTOR_DispatchReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = USBSTOR_DispatchReadWrite;

    //
    // scsi queue ioctl
    //
    DriverObject->MajorFunction[IRP_MJ_SCSI] = USBSTOR_DispatchScsi;

    //
    // pnp processing
    //
    DriverObject->MajorFunction[IRP_MJ_PNP] = USBSTOR_DispatchPnp;

    //
    // power processing
    //
    DriverObject->MajorFunction[IRP_MJ_POWER] = USBSTOR_DispatchPower;

    return STATUS_SUCCESS;
}

