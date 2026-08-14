#pragma once

#include "imfluent.h"

namespace ImFluent
{
    IMGUI_API ImU32 GetColorU32( ImFluentCol idx, float alpha_mul = 1.0f );
    IMGUI_API const ImVec4 & GetStyleColorVec4( ImFluentCol idx );

    IMGUI_API const char * LocalizeGetMsg( ImFluentLocKey key );
    IMGUI_API void LocalizeRegisterEntries( const ImFluentLocEntry * entries, int count );

    IMGUI_API float FluentDpx( float v );
    IMGUI_API ImVec2 FluentDpx( const ImVec2 & v );
} // namespace ImFluent

struct ImFluentStackGuard
{
    ImFluentStackGuard()
        : Colors( 0 )
        , StyleVars( 0 )
        , Fonts( 0 )
        , FluentFonts( 0 )
        , IDs( 0 )
        , ItemWidths( 0 )
        , Disabled( 0 )
        , Groups( 0 )
    {
    }
    ~ImFluentStackGuard()
    {
        Restore();
    }

    void PushStyleColor( ImGuiCol idx, ImU32 col )
    {
        ImGui::PushStyleColor( idx, col );
        ++Colors;
    }
    void PushStyleColor( ImGuiCol idx, const ImVec4 & col )
    {
        ImGui::PushStyleColor( idx, col );
        ++Colors;
    }
    void PushStyleVar( ImGuiStyleVar idx, float v )
    {
        ImGui::PushStyleVar( idx, v );
        ++StyleVars;
    }
    void PushStyleVar( ImGuiStyleVar idx, const ImVec2 & v )
    {
        ImGui::PushStyleVar( idx, v );
        ++StyleVars;
    }
    void PushFont( ImFont * f )
    {
        ImGui::PushFont( f );
        ++Fonts;
    }
    void PushFluentFont( ImFluentTextStyle s )
    {
        ImFluent::PushFont( s );
        ++FluentFonts;
    }
    void PushID( const char * s )
    {
        ImGui::PushID( s );
        ++IDs;
    }
    void PushID( int id )
    {
        ImGui::PushID( id );
        ++IDs;
    }
    void PushID( const void * p )
    {
        ImGui::PushID( p );
        ++IDs;
    }
    void PushItemWidth( float w )
    {
        ImGui::PushItemWidth( w );
        ++ItemWidths;
    }
    void BeginDisabled( bool disabled = true )
    {
        ImGui::BeginDisabled( disabled );
        ++Disabled;
    }
    void BeginGroup()
    {
        ImGui::BeginGroup();
        ++Groups;
    }

    void Restore()
    {
        while ( Groups-- > 0 )
            ImGui::EndGroup();
        while ( Disabled-- > 0 )
            ImGui::EndDisabled();
        while ( ItemWidths-- > 0 )
            ImGui::PopItemWidth();
        while ( IDs-- > 0 )
            ImGui::PopID();
        while ( FluentFonts-- > 0 )
            ImFluent::PopFont();
        while ( Fonts-- > 0 )
            ImGui::PopFont();
        if ( StyleVars > 0 )
        {
            ImGui::PopStyleVar( StyleVars );
            StyleVars = 0;
        }
        if ( Colors > 0 )
        {
            ImGui::PopStyleColor( Colors );
            Colors = 0;
        }
        Groups = Disabled = ItemWidths = IDs = FluentFonts = Fonts = 0;
    }

    void Forget()
    {
        Colors = StyleVars = Fonts = FluentFonts = IDs = ItemWidths = Disabled = Groups = 0;
    }

private:
    ImFluentStackGuard( const ImFluentStackGuard & );
    ImFluentStackGuard & operator=( const ImFluentStackGuard & );

    int Colors;
    int StyleVars;
    int Fonts;
    int FluentFonts;
    int IDs;
    int ItemWidths;
    int Disabled;
    int Groups;
};
